import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BloomUI

/**
 * PersonScreen - one person and the movies and shows they appear in.
 *
 * Browse archetype (docs/style-guide.md): an ambient stage that picks up the
 * focused title's backdrop, no hero. Below the navigation bar: the person's
 * photo beside their name, life dates and birthplace, and a biography with
 * Read More; then poster shelves of their Movies and Shows, newest first,
 * from the live connection (merged spaces route there before the page opens).
 *
 * Keys: Read More (when the biography is long) is the top-most control, then
 * the shelves. Up from the top-most control reaches the navigation bar.
 * Enter opens a title on its details page; Back returns here with focus on
 * that title. Shelves read further pages as focus nears their end.
 */
FocusScope {
    id: root
    focus: true

    /// The canonical person map from a cast card or a people search result.
    required property var personData

    property string navigationId: "person"
    /// Stamped by Main with the live connection; Back skips this page once a
    /// route moves the session to another source.
    property string ownerConnectionId: ""

    signal navigateToMovie(var movieData)
    signal navigateToSeries(var seriesId)

    readonly property var person: viewModel.person || ({})
    readonly property var movies: viewModel.movies || []
    readonly property var shows: viewModel.series || []
    readonly property bool hasTitles: movies.length > 0 || shows.length > 0
    readonly property string biography: person.overview || ""
    property bool biographyExpanded: false

    /// The navigation bar recedes once the page scrolls past the header.
    readonly property bool navigationRecessed: contentFlickable.contentY > Theme.spacingLarge

    // Same request width as Search and Home posters (shared cache entries).
    readonly property int posterRequestWidth: 640
    readonly property int photoWidth: Theme.posterCardWidth
    readonly property int photoHeight: Theme.posterCardHeight
    readonly property real shelfScrollOffset: Math.round(56 * Theme.layoutScale)
    // Read the next page when focus is this close to a shelf's end.
    readonly property int loadMoreThreshold: 8

    /// Last focused control or shelf, restored after a details page or the
    /// navigation bar hands focus back.
    property Item lastFocusTarget: null
    property string stageSource: ""
    property string pendingStageSource: ""

    PersonViewModel {
        id: viewModel
    }

    Component.onCompleted: viewModel.load(root.personData)

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active)
            Qt.callLater(root.restoreLastFocus)
    }

    // The first titles arrive after the page has focus: move into them once
    // loading settles, unless focus is already inside the page or elsewhere.
    Connections {
        target: viewModel
        function onFilmographyChanged() { Qt.callLater(root.focusInitialTarget) }
        function onIsLoadingChanged() { Qt.callLater(root.focusInitialTarget) }
        function onHasErrorChanged() { Qt.callLater(root.focusInitialTarget) }
    }

    SourceRouter {
        id: sourceRouter
    }

    // ========================================
    // Text helpers
    // ========================================

    function formatDate(isoDate) {
        if (!isoDate)
            return ""
        const parts = String(isoDate).split("-")
        if (parts.length !== 3)
            return ""
        const date = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]))
        return isNaN(date.getTime()) ? "" : date.toLocaleDateString(Qt.locale(), Locale.LongFormat)
    }

    function yearOf(isoDate) {
        return isoDate ? String(isoDate).substring(0, 4) : ""
    }

    // "Born 25 April 1940 in New York City", or the life span when the
    // person has died.
    function lifeLine() {
        const birth = formatDate(person.birthDate)
        const death = formatDate(person.deathDate)
        const place = person.birthplace || ""
        if (death !== "") {
            const span = birth !== "" ? qsTr("%1 – %2").arg(birth).arg(death)
                                      : qsTr("Died %1").arg(death)
            return place !== "" ? qsTr("%1  ·  %2").arg(span).arg(place) : span
        }
        if (birth !== "")
            return place !== "" ? qsTr("Born %1 in %2").arg(birth).arg(place) : qsTr("Born %1").arg(birth)
        return place !== "" ? qsTr("Born in %1").arg(place) : ""
    }

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

    function artworkUrlFromRef(artwork, width) {
        if (!artwork || !artwork.itemId)
            return ""
        return LibraryService.getCachedArtworkUrlFromRef(artwork, width || 0)
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
    function openTitle(item) {
        if (!item)
            return
        if (item.mediaType === "Series")
            sourceRouter.run(item.connectionId || "", function() { root.navigateToSeries(item.itemId) })
        else if (item.mediaType === "Movie")
            sourceRouter.run(item.connectionId || "", function() { root.navigateToMovie(item) })
    }

    // ========================================
    // Focus and scrolling
    // ========================================

    // Focusable rows in visual order (visible ones only).
    function focusTargets() {
        var targets = []
        if (readMoreButton.visible)
            targets.push(readMoreButton)
        if (errorState.visible)
            targets.push(errorState)
        var lists = [moviesList, showsList]
        for (var i = 0; i < lists.length; i++) {
            if (lists[i].parent.visible && lists[i].count > 0)
                targets.push(lists[i])
        }
        return targets
    }

    function focusTarget(target) {
        if (target === errorState)
            errorState.focusAction()
        else
            target.forceActiveFocus()
    }

    // Up/Down between rows. Up from the top row is left unhandled so it
    // reaches the navigation bar.
    function moveFocus(from, delta) {
        var targets = focusTargets()
        var index = targets.indexOf(from)
        var next = index + delta
        if (index < 0 || next < 0 || next >= targets.length)
            return false
        focusTarget(targets[next])
        return true
    }

    function hasFocusInside() {
        var item = root.Window.activeFocusItem
        while (item) {
            if (item === contentColumn)
                return true
            item = item.parent
        }
        return false
    }

    // The first shelf once titles arrive, else Read More or Retry.
    function focusInitialTarget() {
        if (!root.activeFocus || hasFocusInside() || (viewModel.isLoading && !root.hasTitles))
            return
        var lists = [moviesList, showsList]
        for (var i = 0; i < lists.length; i++) {
            if (lists[i].parent.visible && lists[i].count > 0) {
                lists[i].forceActiveFocus()
                return
            }
        }
        var targets = focusTargets()
        if (targets.length > 0)
            focusTarget(targets[0])
    }

    function restoreLastFocus() {
        var target = lastFocusTarget
        if (target && target.visible && (target === readMoreButton
                                         || (target.parent.visible && target.count > 0))) {
            target.forceActiveFocus()
            return
        }
        root.forceActiveFocus()
        focusInitialTarget()
    }

    function saveFocusForNavigation() {
        // lastFocusTarget already tracks the controls and shelves.
    }

    function restoreFocusFromNavigation() {
        restoreLastFocus()
    }

    function scrollToItem(item, toTop) {
        if (!item)
            return
        var y = toTop ? 0 : item.mapToItem(contentFlickable.contentItem, 0, 0).y - root.shelfScrollOffset
        var maxY = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
        scrollAnimation.to = Math.max(0, Math.min(maxY, y))
        scrollAnimation.restart()
    }

    function loadMoreIfNear(list) {
        if (viewModel.hasMore && list.currentIndex >= list.count - root.loadMoreThreshold)
            viewModel.loadMore()
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
    // Cards
    // ========================================

    Component {
        id: titleCardDelegate

        MediaCard {
            id: titleCard
            required property var modelData
            required property int index
            variant: "poster"
            artworkSources: [root.artworkUrlFromRef(modelData.primaryArtwork, root.posterRequestWidth)]
            title: modelData.name || ""
            subtitle: root.titleSubtitle(modelData)
            placeholderIcon: modelData.mediaType === "Series" ? Icons.tvShows : Icons.movie
            item: modelData
            showWatchState: modelData.mediaType === "Series"
                            || (modelData.mediaType === "Movie" && !!modelData.watched)
            unplayedCount: modelData.mediaType === "Series" ? (modelData.unplayedItemCount || 0) : 0
            watched: !!modelData.watched
            focused: ListView.isCurrentItem && ListView.view.activeFocus
            onFocusedChanged: if (focused) root.showBackdropFor(modelData)
            onClicked: {
                var list = titleCard.ListView.view
                list.currentIndex = index
                list.forceActiveFocus()
                root.openTitle(modelData)
            }
            Accessible.description: qsTr("Details for %1").arg(modelData.name || "")
        }
    }

    // ========================================
    // Content
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

            // Photo beside name, life line and biography.
            RowLayout {
                id: headerRow
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                spacing: Theme.spacingXLarge

                Item {
                    Layout.preferredWidth: root.photoWidth
                    Layout.preferredHeight: root.photoHeight
                    Layout.alignment: Qt.AlignTop

                    Image {
                        id: photo
                        anchors.fill: parent
                        source: root.artworkUrlFromRef(root.person.artwork, 640)
                        sourceSize.width: 640
                        fillMode: Image.PreserveAspectCrop
                        verticalAlignment: Image.AlignTop
                        asynchronous: true
                        cache: true
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            maskEnabled: true
                            maskSource: photoMask
                        }
                        Accessible.ignored: true
                    }

                    Item {
                        id: photoMask
                        anchors.fill: parent
                        visible: false
                        layer.enabled: true
                        layer.smooth: true

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.imageRadius
                            color: "white"
                        }
                    }

                    ArtworkPlaceholder {
                        anchors.fill: parent
                        visible: photo.status !== Image.Ready
                        loading: photo.status === Image.Loading
                        iconGlyph: Icons.person
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Theme.heroCopyMaxWidth
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacingMedium

                    PageHeader {
                        Layout.fillWidth: true
                        title: root.person.name || ""
                        detail: root.lifeLine()
                    }

                    Text {
                        id: biographyText
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: root.biography
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                        opacity: 0.86
                        lineHeight: 1.12
                        wrapMode: Text.WordWrap
                        maximumLineCount: root.biographyExpanded ? 40 : 5
                        elide: Text.ElideRight
                    }

                    // Read More sits under the biography, its label aligned
                    // with the text.
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: readMoreButton.implicitHeight
                        visible: biographyText.visible
                                 && (biographyText.truncated || root.biographyExpanded)

                        ActionButton {
                            id: readMoreButton
                            x: -Theme.spacingMedium
                            variant: "ghost"
                            implicitHeight: Theme.buttonHeightSmall
                            sidePadding: Theme.spacingMedium
                            labelPixelSize: Theme.fontSizeSmall
                            text: root.biographyExpanded ? qsTr("Show Less") : qsTr("Read More")
                            iconGlyph: root.biographyExpanded ? Icons.expandLess : Icons.expandMore
                            accessibleLabel: root.biographyExpanded ? qsTr("Show less of the biography")
                                                                    : qsTr("Read the whole biography")
                            onClicked: root.biographyExpanded = !root.biographyExpanded
                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root.lastFocusTarget = readMoreButton
                                    root.scrollToItem(headerRow, true)
                                }
                            }
                            Keys.onReturnPressed: (event) => { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                            Keys.onEnterPressed: (event) => { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                            Keys.onDownPressed: (event) => { event.accepted = root.moveFocus(readMoreButton, 1) }
                            // Top-most control: Up goes on to the navigation bar.
                            Keys.onUpPressed: (event) => { event.accepted = false }
                            Keys.onLeftPressed: (event) => { event.accepted = true }
                            Keys.onRightPressed: (event) => { event.accepted = true }
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
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: viewModel.isLoading && !root.hasTitles
                kind: "loading"
                message: qsTr("Loading filmography…")
            }

            StateMessage {
                id: errorState
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: viewModel.hasError && !root.hasTitles
                kind: "error"
                title: qsTr("Couldn't load this person's titles")
                message: viewModel.errorMessage
                actionText: qsTr("Try again")
                onActionTriggered: viewModel.reload()
                onActiveFocusChanged: if (activeFocus) root.lastFocusTarget = null
                Keys.onUpPressed: (event) => { event.accepted = root.moveFocus(errorState, -1) }
            }

            StateMessage {
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: !viewModel.isLoading && !viewModel.hasError && !root.hasTitles
                kind: "empty"
                iconGlyph: Icons.person
                title: qsTr("Nothing to watch yet")
                message: qsTr("None of your movies or shows feature %1.").arg(root.person.name || qsTr("this person"))
            }

            // ========================================
            // Shelves
            // ========================================

            ColumnLayout {
                id: moviesSection
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing
                spacing: Theme.sectionTitleSpacing
                visible: root.movies.length > 0

                SectionHeader {
                    text: qsTr("Movies")
                    detail: String(root.movies.length)
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
                    model: root.movies
                    delegate: titleCardDelegate
                    onCurrentIndexChanged: root.loadMoreIfNear(moviesList)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveFocus(moviesList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveFocus(moviesList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.movies.length)
                            root.openTitle(root.movies[currentIndex])
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.movies.length)
                            root.openTitle(root.movies[currentIndex])
                        event.accepted = true
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root.lastFocusTarget = moviesList
                            root.scrollToItem(moviesSection, false)
                            root.loadMoreIfNear(moviesList)
                        }
                    }
                }
            }

            ColumnLayout {
                id: showsSection
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing
                spacing: Theme.sectionTitleSpacing
                visible: root.shows.length > 0

                SectionHeader {
                    text: qsTr("Shows")
                    detail: String(root.shows.length)
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: showsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: root.shows
                    delegate: titleCardDelegate
                    onCurrentIndexChanged: root.loadMoreIfNear(showsList)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveFocus(showsList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveFocus(showsList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.shows.length)
                            root.openTitle(root.shows[currentIndex])
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.shows.length)
                            root.openTitle(root.shows[currentIndex])
                        event.accepted = true
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root.lastFocusTarget = showsList
                            root.scrollToItem(showsSection, false)
                            root.loadMoreIfNear(showsList)
                        }
                    }
                }
            }

            // More titles are loading at the end of a shelf.
            BusyIndicator {
                Layout.leftMargin: Theme.pageMarginX
                Layout.topMargin: Theme.spacingMedium
                Layout.preferredWidth: Math.round(40 * Theme.layoutScale)
                Layout.preferredHeight: Layout.preferredWidth
                visible: viewModel.loadingMore
                running: visible
            }

            Item { Layout.preferredHeight: Theme.pageMarginBottom }
        }
    }

    WheelStepScroller {
        anchors.fill: contentFlickable
        target: contentFlickable
        stepPx: Math.round(120 * Theme.layoutScale)
    }
}
