pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * UpcomingScreen - the episodes airing on the live connection from today
 * through the next 30 days, one poster shelf per day
 * (docs/style-guide.md, Upcoming).
 *
 * Opened from the navigation bar's Upcoming tab. UpcomingViewModel reads
 * the calendar and groups it into the viewer's local days ("Today",
 * "Tomorrow", then the weekday and date). On Silo a row of presets
 * (Following, Trending, All) sits in the header and is the top-most
 * control. Left/Right walk a day, Up/Down move between days, Enter opens
 * the series page (an airing never offers Play), Back returns to the same
 * card, and Up from the top reaches the presets, then the navigation bar.
 */
FocusScope {
    id: root
    focus: true
    property string navigationId: "upcoming"
    /// Stamped by Main with the live connection; Back skips this page once a
    /// route moves the session to another source.
    property string ownerConnectionId: ""

    // Main's details navigation (connectDetailNavigation).
    signal navigateToMovie(var movieData)
    signal navigateToSeries(var seriesId)

    readonly property bool navigationRecessed: dayList.contentY > Theme.spacingLarge
    // Same request width as Home and Search posters (shared cache entries).
    readonly property int posterRequestWidth: 640
    readonly property bool filtersShown: upcoming.filterable

    // The card focus returns to, by day and ID, then by position.
    property string lastDate: ""
    property string lastItemId: ""
    property int lastRow: 0
    property int lastColumn: 0

    UpcomingViewModel {
        id: upcoming
        onCountChanged: Qt.callLater(root.restoreLastFocusIfIdle)
        onIsLoadingChanged: {
            if (!upcoming.isLoading)
                Qt.callLater(root.restoreLastFocusIfIdle)
        }
        onHasErrorChanged: Qt.callLater(root.restoreLastFocusIfIdle)
    }

    SourceRouter {
        id: sourceRouter
        onFailed: function(message) {
            toast.show(qsTr("Couldn't switch to that show's server. %1")
                       .arg(message || qsTr("The server isn't reachable.")))
        }
    }

    Component.onCompleted: upcoming.load()

    StackView.onStatusChanged: {
        if (StackView.status !== StackView.Active)
            return
        // Silo's poster URLs expire, and the day may have moved on.
        upcoming.refreshIfStale()
        Qt.callLater(root.restoreLastFocus)
    }

    // ========================================
    // Text
    // ========================================

    function headerDetail() {
        if (upcoming.count === 0)
            return ""
        var episodes = upcoming.airingCount === 1
                ? qsTr("1 episode") : qsTr("%1 episodes").arg(upcoming.airingCount)
        return qsTr("%1 in the next 30 days").arg(episodes)
    }

    function dayDetail(airings) {
        var count = airings ? airings.length : 0
        return count === 1 ? qsTr("1 episode") : qsTr("%1 episodes").arg(count)
    }

    function cardDescription(airing) {
        var parts = [airing.seriesName || ""]
        if (airing.episodeLabel)
            parts.push(airing.episodeLabel)
        if (airing.episodeTitle)
            parts.push(airing.episodeTitle)
        if (airing.timeLabel)
            parts.push(qsTr("airs at %1").arg(airing.timeLabel))
        if (airing.badgeLabel)
            parts.push(airing.badgeLabel)
        return qsTr("%1. Opens the series.").arg(parts.join(", "))
    }

    // ========================================
    // Opening a series
    // ========================================

    // Airings belong to the live connection; the router is a no-op there
    // and keeps the merged-space rule in one place.
    function openAiring(airing) {
        if (!airing || !airing.seriesId)
            return
        sourceRouter.run(airing.connectionId || "", function() {
            root.navigateToSeries(airing.seriesId)
        })
    }

    // ========================================
    // Focus
    // ========================================

    function rememberCard(date, itemId, row, column) {
        lastDate = date || ""
        lastItemId = itemId || ""
        lastRow = Math.max(0, row)
        lastColumn = Math.max(0, column)
    }

    function shelfAt(row) {
        var day = dayList.itemAtIndex(row)
        return day ? day.shelf : null
    }

    function cardHasFocus() {
        var shelf = shelfAt(dayList.currentIndex)
        return !!shelf && shelf.activeFocus
    }

    // Focus moves to the card at column of the day at row (both clamped).
    function focusDay(row, column) {
        if (upcoming.count === 0)
            return false
        row = Math.max(0, Math.min(row, upcoming.count - 1))
        dayList.currentIndex = row
        if (!shelfAt(row))
            dayList.positionViewAtIndex(row, ListView.Contain)
        var shelf = shelfAt(row)
        if (!shelf || shelf.count === 0)
            return false
        shelf.currentIndex = Math.max(0, Math.min(column, shelf.count - 1))
        shelf.forceActiveFocus()
        return true
    }

    // Up and Down between days keep the column where the day has one.
    function stepDay(row, delta, column) {
        var target = row + delta
        if (target < 0 || target >= upcoming.count)
            return false
        return focusDay(target, column)
    }

    function focusFilters() {
        for (var i = 0; i < filterRepeater.count; ++i) {
            var button = filterRepeater.itemAt(i)
            if (button && button.modelData.id === upcoming.filter) {
                button.forceActiveFocus()
                return true
            }
        }
        return false
    }

    function filtersHaveFocus() {
        for (var i = 0; i < filterRepeater.count; ++i) {
            var button = filterRepeater.itemAt(i)
            if (button && button.activeFocus)
                return true
        }
        return false
    }

    // Down from the presets: the remembered card, else the state message's
    // action.
    function focusContent() {
        if (upcoming.count > 0)
            return focusDay(lastRow, lastColumn)
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

    function restoreLastFocus() {
        if (upcoming.count > 0) {
            var row = upcoming.indexOfDay(lastDate)
            if (row < 0)
                row = Math.min(lastRow, upcoming.count - 1)
            var column = upcoming.indexOfAiring(row, lastItemId)
            if (column < 0)
                column = lastColumn
            if (focusDay(row, column))
                return
        }
        if (focusContent())
            return
        if (filtersShown && focusFilters())
            return
        root.forceActiveFocus()
    }

    // After a read or a regroup, focus returns to the cards; never away from
    // the navigation bar, a preset, or a card that kept it.
    function restoreLastFocusIfIdle() {
        if (root.StackView.status !== StackView.Active || upcoming.isLoading)
            return
        if (!root.activeFocus || filtersHaveFocus() || cardHasFocus()
                || errorState.activeFocus || emptyState.activeFocus)
            return
        restoreLastFocus()
    }

    function saveFocusForNavigation() {
        // Cards remember themselves as focus moves.
    }

    function restoreFocusFromNavigation() {
        restoreLastFocus()
    }

    // ========================================
    // Stage
    // ========================================

    // The calendar has posters only, so the stage keeps its accent glows.
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
            title: qsTr("Upcoming")
            detail: root.headerDetail()

            // Silo's presets. Up is left to Main, which enters the
            // navigation bar; Down enters the days.
            Repeater {
                id: filterRepeater
                model: root.filtersShown ? upcoming.filters : []

                ActionButton {
                    id: filterButton
                    required property var modelData
                    required property int index
                    variant: "secondary"
                    text: modelData.text
                    selected: upcoming.filter === modelData.id
                    Layout.preferredHeight: Theme.buttonHeightMedium
                    Accessible.name: qsTr("Show %1").arg(modelData.text)
                    Accessible.description: selected ? qsTr("Selected") : ""
                    Keys.onLeftPressed: function(event) {
                        var previous = filterRepeater.itemAt(index - 1)
                        if (previous)
                            previous.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onRightPressed: function(event) {
                        var next = filterRepeater.itemAt(index + 1)
                        if (next)
                            next.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onDownPressed: function(event) {
                        event.accepted = root.focusContent()
                    }
                    Keys.onReturnPressed: function(event) {
                        if (!event.isAutoRepeat)
                            filterButton.clicked()
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (!event.isAutoRepeat)
                            filterButton.clicked()
                        event.accepted = true
                    }
                    onClicked: {
                        // Another preset's days start at their first card.
                        if (upcoming.filter !== modelData.id) {
                            root.rememberCard("", "", 0, 0)
                            dayList.positionViewAtBeginning()
                        }
                        upcoming.filter = modelData.id
                    }
                }
            }
        }

        StateMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: upcoming.isLoading && upcoming.count === 0
            kind: "loading"
            message: qsTr("Loading…")
        }

        StateMessage {
            id: errorState
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: !upcoming.isLoading && upcoming.hasError && upcoming.count === 0
            kind: "error"
            title: qsTr("Couldn't load what's coming up")
            message: upcoming.errorMessage
            actionText: qsTr("Try again")
            onActionTriggered: upcoming.reload()
            Keys.onUpPressed: function(event) {
                event.accepted = root.filtersShown && root.focusFilters()
            }
        }

        StateMessage {
            id: emptyState
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: !upcoming.isLoading && !upcoming.hasError && upcoming.count === 0
            kind: "empty"
            iconGlyph: Icons.calendarMonth
            title: upcoming.emptyTitle
            message: upcoming.emptyMessage
            actionText: upcoming.emptyActionFilter !== "" ? qsTr("Show all") : ""
            onActionTriggered: upcoming.filter = upcoming.emptyActionFilter
            Keys.onUpPressed: function(event) {
                event.accepted = root.filtersShown && root.focusFilters()
            }
        }

        ListView {
            id: dayList
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Hidden by opacity: toggling visible loops the wheel scroller's
            // enabled binding.
            opacity: upcoming.count > 0 ? 1 : 0
            model: upcoming
            spacing: Theme.sectionSpacing
            clip: true
            // Keys belong to the shelves: Up from the first day must reach
            // the presets or Main.
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            bottomMargin: Theme.pageMarginBottom
            highlightRangeMode: InputModeManager.pointerActive ? ListView.NoHighlightRange
                                                               : ListView.ApplyRange
            preferredHighlightBegin: 0
            preferredHighlightEnd: height
            highlightMoveDuration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0
            cacheBuffer: Math.max(0, height)
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Upcoming episodes by day")

            delegate: ColumnLayout {
                id: dayDelegate
                required property int index
                required property string date
                required property string title
                required property var airings
                readonly property alias shelf: dayShelf

                width: dayList.width
                spacing: Theme.sectionTitleSpacing

                SectionHeader {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    text: dayDelegate.title
                    detail: root.dayDetail(dayDelegate.airings)
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: dayShelf
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: dayDelegate.airings
                    Accessible.name: dayDelegate.title

                    function remember() {
                        var airing = dayDelegate.airings[currentIndex]
                        root.rememberCard(dayDelegate.date, airing ? airing.itemId : "",
                                          dayDelegate.index, currentIndex)
                    }

                    onCurrentIndexChanged: if (activeFocus) remember()
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            dayList.currentIndex = dayDelegate.index
                            remember()
                        }
                    }

                    delegate: MediaCard {
                        id: airingCard
                        required property var modelData
                        required property int index
                        variant: "poster"
                        artworkSources: [modelData.seriesPrimaryArtwork
                                         ? LibraryService.getCachedArtworkUrlFromRef(
                                               modelData.seriesPrimaryArtwork, root.posterRequestWidth)
                                         : ""]
                        title: modelData.seriesName || modelData.episodeTitle || ""
                        subtitle: modelData.subtitle || ""
                        placeholderIcon: Icons.tvShows
                        // An episode the profile already watched (Silo).
                        showWatchState: !!modelData.watched
                        watched: !!modelData.watched
                        focused: ListView.isCurrentItem && dayShelf.activeFocus
                        onClicked: {
                            dayShelf.currentIndex = index
                            dayShelf.forceActiveFocus()
                            root.openAiring(modelData)
                        }
                        Accessible.description: root.cardDescription(modelData)
                    }

                    Keys.onUpPressed: function(event) {
                        if (root.stepDay(dayDelegate.index, -1, currentIndex)) {
                            event.accepted = true
                            return
                        }
                        // The first day: the presets, else Main's bar.
                        event.accepted = dayDelegate.index === 0 && root.filtersShown
                                && root.focusFilters()
                    }
                    Keys.onDownPressed: function(event) {
                        root.stepDay(dayDelegate.index, 1, currentIndex)
                        event.accepted = true
                    }
                    Keys.onReturnPressed: function(event) {
                        if (!event.isAutoRepeat)
                            root.openAiring(dayDelegate.airings[currentIndex])
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (!event.isAutoRepeat)
                            root.openAiring(dayDelegate.airings[currentIndex])
                        event.accepted = true
                    }
                }
            }

            WheelStepScroller {
                anchors.fill: parent
                target: dayList
                stepPx: Theme.posterCardHeight * 0.45
                z: 2
            }
        }
    }

    ToastNotification {
        id: toast
        z: 10
    }
}
