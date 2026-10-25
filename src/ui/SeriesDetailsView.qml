import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import BloomUI

FocusScope {
    id: root

    property string seriesId: ""
    property int pendingSeasonsGridIndex: -1
    property var pendingReturnState: null
    property bool restorePendingReturnState: false
    property var seerrRecommendationCacheStore: null
    property int seerrRecommendationCacheTtlMs: 10 * 60 * 1000
    readonly property bool isRestoringReturnFocus: restoringFocusFromSidebar
                                                 || restoringFocusFromReturnState
                                                 || suppressHeroAutofocus
                                                 || hasPendingReturnStateForCurrentSeries()

    property var seerrRecommendedItems: []
    property bool seerrRecommendationsLoading: false
    property string seerrPendingTmdbId: ""
    property string seerrLoadedTmdbId: ""

    readonly property string seriesName: SeriesDetailsViewModel.title
    readonly property string seriesOverview: SeriesDetailsViewModel.overview
    readonly property bool hasNextEpisode: SeriesDetailsViewModel.hasNextEpisode
    readonly property bool isWatched: SeriesDetailsViewModel.isWatched
    readonly property bool isFavorite: SeriesDetailsViewModel.isFavorite
    readonly property string tmdbId: SeriesDetailsViewModel.tmdbId
    readonly property string logoUrl: SeriesDetailsViewModel.logoUrl
    readonly property string posterUrl: SeriesDetailsViewModel.posterUrl
    readonly property string backdropUrl: SeriesDetailsViewModel.backdropUrl
    readonly property bool isLoading: SeriesDetailsViewModel.isLoading
    readonly property var castAndCrew: SeriesDetailsViewModel.people || []
    readonly property var libraryRecommendations: SeriesDetailsViewModel.similarItems || []
    readonly property bool libraryRecommendationsLoading: SeriesDetailsViewModel.similarItemsLoading
    readonly property bool seerrConfigured: typeof SeerrService !== "undefined" && SeerrService !== null && SeerrService.configured
    readonly property string seerrMediaType: "tv" // SeriesDetailsView only issues TV similar-title requests.
    readonly property var externalRatings: SeriesDetailsViewModel.mdbListRatings || ({})

    readonly property int heroPosterWidth: Math.round(320 * Theme.layoutScale)
    readonly property int heroPosterHeight: Math.round(heroPosterWidth * 1.5)
    readonly property int heroPanelPadding: Theme.spacingXLarge
    readonly property int heroActionsBottomSpacing: Theme.spacingMedium
    readonly property int railWidth: Math.round(360 * Theme.layoutScale)
    readonly property int peopleCardWidth: Math.round(176 * Theme.layoutScale)
    readonly property int peopleCardHeight: Math.round(320 * Theme.layoutScale)
    readonly property int recommendationCardWidth: Math.round(236 * Theme.layoutScale)
    readonly property int recommendationCardHeight: recommendationCardWidth + Math.round(recommendationCardWidth * 0.5) + Math.round(74 * Theme.layoutScale)
    readonly property int shelfEdgePadding: Math.round(14 * Theme.layoutScale)

    signal navigateToSeasons(int seasonIndex)
    signal playNextEpisode(string episodeId, var startPositionMs)
    signal navigateToEpisode(var episodeData)
    signal itemSelected(var itemData)
    signal backRequested()
    signal returnStateConsumed()

    focus: true

    component RecommendationPosterCard: Item {
        id: recommendationCard

        required property var itemData
        property bool isFocused: false
        property bool isHovered: InputModeManager.pointerActive && recommendationMouseArea.containsMouse
        readonly property bool isSeerr: itemData.source === "seerr"
        readonly property string posterSource: {
            if (isSeerr) {
                return itemData.imageUrl || ""
            }
            const artwork = itemData.primaryArtwork
            if (artwork && artwork.itemId) {
                return LibraryService.getCachedArtworkUrlFromRef(artwork, 420)
            }
            return ""
        }
        readonly property string title: itemData.name || ""
        readonly property string subtitle: {
            const productionYear = itemData.productionYear
            const premiereDate = itemData.premiereDate
            if (productionYear) {
                return String(productionYear)
            }
            if (premiereDate) {
                const date = new Date(premiereDate)
                if (!isNaN(date.getTime())) {
                    return String(date.getFullYear())
                }
            }
            return ""
        }

        width: root.recommendationCardWidth
        height: root.recommendationCardHeight
        scale: isFocused ? 1.035 : (isHovered ? 1.015 : 1.0)
        z: isFocused ? 2 : 0
        transformOrigin: Item.Center
        Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

        signal clicked()

        Column {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Rectangle {
                id: recommendationPosterContainer
                anchors.horizontalCenter: parent.horizontalCenter
                width: root.recommendationCardWidth
                height: Math.round(width * 1.5)
                radius: Theme.imageRadius
                color: "transparent"
                clip: false

                Image {
                    id: recommendationPosterImage
                    anchors.fill: parent
                    source: recommendationCard.posterSource
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true

                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: recommendationPosterMask
                    }
                }

                Item {
                    id: recommendationPosterMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true
                    layer.smooth: true

                    Rectangle {
                        anchors.centerIn: parent
                        width: recommendationPosterImage.paintedWidth
                        height: recommendationPosterImage.paintedHeight
                        radius: Theme.imageRadius
                        color: "white"
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.imageRadius
                    color: Theme.artworkPlaceholder
                    visible: recommendationPosterImage.status !== Image.Ready

                    Text {
                        anchors.centerIn: parent
                        text: recommendationCard.itemData.mediaType === "Series" ? Icons.tvShows : Icons.movie
                        font.family: Theme.fontIcon
                        font.pixelSize: Math.round(56 * Theme.layoutScale)
                        color: Theme.textSecondary
                    }
                }

                Item {
                    width: recommendationPosterImage.paintedWidth
                    height: recommendationPosterImage.paintedHeight
                    anchors.centerIn: parent

                    PosterBadgeOverlay {
                        anchors.fill: parent
                        item: recommendationCard.itemData
                    }

                    FocusRing {
                        active: recommendationCard.isFocused
                        radius: Theme.imageRadius
                    }
                }
            }

            Item {
                width: root.recommendationCardWidth
                height: recommendationTitleLabel.implicitHeight
                anchors.horizontalCenter: parent.horizontalCenter

                ScrollingCardLabel {
                    id: recommendationTitleLabel
                    anchors.fill: parent
                    text: recommendationCard.title
                    fontPixelSize: Theme.fontSizeSmall
                    fontWeight: Font.DemiBold
                    textColor: Theme.textPrimary
                    active: recommendationCard.isFocused
                }
            }

            Item {
                width: root.recommendationCardWidth
                height: recommendationSubtitleLabel.implicitHeight
                anchors.horizontalCenter: parent.horizontalCenter
                visible: recommendationSubtitleLabel.text !== ""

                ScrollingCardLabel {
                    id: recommendationSubtitleLabel
                    anchors.fill: parent
                    text: recommendationCard.subtitle
                    fontPixelSize: Theme.fontSizeSmall
                    fontWeight: Font.Normal
                    textColor: Theme.textSecondary
                    active: recommendationCard.isFocused
                }
            }
        }

        MouseArea {
            id: recommendationMouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: recommendationCard.clicked()
        }
    }

    function focusTarget(target) {
        if (!target || !target.visible) {
            return
        }
        if (typeof target.focusCurrentOrFirst === "function") {
            target.focusCurrentOrFirst()
            return
        }
        if (typeof target.forceActiveFocus === "function") {
            target.forceActiveFocus()
        }
    }

    function ensureItemVisible(item, topPadding, bottomPadding) {
        if (!item) {
            return
        }

        const pos = item.mapToItem(contentColumn, 0, 0)
        const itemTop = pos.y
        const itemBottom = itemTop + item.height
        const viewportTop = contentFlickable.contentY
        const viewportBottom = viewportTop + contentFlickable.height
        const topInset = topPadding !== undefined ? topPadding : Math.round(48 * Theme.layoutScale)
        const bottomInset = bottomPadding !== undefined ? bottomPadding : Math.round(96 * Theme.layoutScale)
        const maxScroll = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)

        if (itemTop < viewportTop + topInset) {
            contentFlickable.contentY = Math.max(0, itemTop - topInset)
        } else if (itemBottom > viewportBottom - bottomInset) {
            contentFlickable.contentY = Math.min(maxScroll, itemBottom - contentFlickable.height + bottomInset)
        }
    }

    function ensureTopVisible() {
        if (contentFlickable.contentY > 0) {
            contentFlickable.contentY = 0
        }
    }

    function focusFirstLowerSection() {
        if (seasonsGrid.count > 0) {
            focusTarget(seasonsGrid)
        } else if (castSection.visible) {
            focusTarget(castSection)
        } else if (libraryRecommendationsSection.visible) {
            focusTarget(libraryRecommendationsSection)
        } else if (seerrRecommendationsSection.visible) {
            focusTarget(seerrRecommendationsSection)
        }
    }

    function nextSectionAfterSeasons() {
        if (castSection.visible) return castSection
        if (libraryRecommendationsSection.visible) return libraryRecommendationsSection
        if (seerrRecommendationsSection.visible) return seerrRecommendationsSection
        return null
    }

    function nextSectionAfterCast() {
        if (libraryRecommendationsSection.visible) return libraryRecommendationsSection
        if (seerrRecommendationsSection.visible) return seerrRecommendationsSection
        return null
    }

    function nextSectionAfterLibraryRecommendations() {
        if (seerrRecommendationsSection.visible) return seerrRecommendationsSection
        return null
    }

    function nextSectionAfterSeerrRecommendations() {
        return null
    }

    function formatSeriesYears() {
        const startYear = SeriesDetailsViewModel.productionYear
        const endDate = SeriesDetailsViewModel.endDate
        const endYear = endDate && !isNaN(endDate.getTime()) ? endDate.getFullYear() : 0

        if (startYear <= 0) {
            return ""
        }
        if (SeriesDetailsViewModel.status === "Ended" && endYear > 0) {
            return startYear + " - " + endYear
        }
        return startYear + " -"
    }

    function formatCounts() {
        const seasons = SeriesDetailsViewModel.seasonCount
        const episodes = SeriesDetailsViewModel.recursiveItemCount
        const parts = []
        if (seasons > 0) parts.push(seasons + (seasons === 1 ? " Season" : " Seasons"))
        if (episodes > 0) parts.push(episodes + (episodes === 1 ? " Episode" : " Episodes"))
        return parts.join(" \u00b7 ")
    }

    function creatorsSummary() {
        const people = castAndCrew || []
        const names = []
        for (let i = 0; i < people.length; ++i) {
            const person = people[i]
            const type = String(person.kind || "").toLowerCase()
            if (type === "director" || type === "writer" || type === "producer" || type === "creator") {
                names.push(person.name)
            }
            if (names.length === 3) {
                break
            }
        }
        return names.join(", ")
    }

    function visibleExternalRatings() {
        const ratings = externalRatings["ratings"] || []
        const filtered = []
        for (let i = 0; i < ratings.length; ++i) {
            const rating = ratings[i]
            if (!rating) {
                continue
            }
            const value = rating.value
            const score = rating.score
            if ((value === undefined || value === null || value === "" || value === 0 || value === "0")
                    && (score === undefined || score === null || score === "" || score === 0 || score === "0")) {
                continue
            }
            filtered.push(rating)
        }
        return filtered
    }

    function itemIsDescendant(item, ancestor) {
        let current = item
        while (current) {
            if (current === ancestor) {
                return true
            }
            current = current.parent
        }
        return false
    }

    function hasPendingReturnStateForCurrentSeries() {
        return restorePendingReturnState
                && pendingReturnState
                && String(pendingReturnState.seriesId || "") === String(seriesId || "")
    }

    function currentFocusArea() {
        const activeItem = root.Window.activeFocusItem
        if (!activeItem) {
            return "hero"
        }
        if (itemIsDescendant(activeItem, playButton)
                || itemIsDescendant(activeItem, favoriteButton)
                || itemIsDescendant(activeItem, contextMenuButton)) {
            return "hero"
        }
        if (itemIsDescendant(activeItem, seasonsGrid)) {
            return "seasons"
        }
        if (itemIsDescendant(activeItem, castList)) {
            return "cast"
        }
        if (itemIsDescendant(activeItem, libraryRecommendationsList)) {
            return "libraryRecommendations"
        }
        if (itemIsDescendant(activeItem, seerrRecommendationsList)) {
            return "seerrRecommendations"
        }
        return "hero"
    }

    function currentFocusIndex() {
        const area = currentFocusArea()
        const activeItem = root.Window.activeFocusItem

        if (area === "hero") {
            if (itemIsDescendant(activeItem, favoriteButton)) return 1
            if (itemIsDescendant(activeItem, contextMenuButton)) return 2
            return 0
        }
        if (area === "seasons") return Math.max(0, seasonsGrid.currentIndex)
        if (area === "cast") return Math.max(0, castList.currentIndex)
        if (area === "libraryRecommendations") return Math.max(0, libraryRecommendationsList.currentIndex)
        if (area === "seerrRecommendations") return Math.max(0, seerrRecommendationsList.currentIndex)
        return 0
    }

    function saveReturnState() {
        return {
            seriesId: seriesId,
            focusArea: currentFocusArea(),
            focusIndex: currentFocusIndex(),
            seasonsGridIndex: Math.max(0, seasonsGrid.currentIndex),
            contentY: contentFlickable.contentY,
            seerrTmdbId: String(tmdbId || seerrLoadedTmdbId || ""),
            seerrRecommendations: (seerrRecommendedItems || []).slice()
        }
    }

    function focusCurrentViewItem(view) {
        if (!view) {
            return false
        }
        if (view.currentItem && typeof view.currentItem.forceActiveFocus === "function") {
            view.currentItem.forceActiveFocus()
            return true
        }
        if (typeof view.forceActiveFocus === "function") {
            view.forceActiveFocus()
            return true
        }
        return false
    }

    function restoreFocusToArea(area, index) {
        const targetIndex = Math.max(0, index || 0)

        if (area === "hero") {
            if (targetIndex === 1 && favoriteButton.visible) {
                favoriteButton.forceActiveFocus()
            } else if (targetIndex === 2 && contextMenuButton.visible) {
                contextMenuButton.forceActiveFocus()
            } else {
                playButton.forceActiveFocus()
            }
            return true
        }

        if (area === "seasons" && seasonsGrid.count > 0) {
            seasonsGrid.currentIndex = Math.min(targetIndex, seasonsGrid.count - 1)
            seasonsGrid.positionViewAtIndex(seasonsGrid.currentIndex, GridView.Contain)
            ensureItemVisible(seasonsGrid, Math.round(80 * Theme.layoutScale), Math.round(120 * Theme.layoutScale))
            return focusCurrentViewItem(seasonsGrid)
        }

        if (area === "cast" && castSection.visible && castList.count > 0) {
            castList.currentIndex = Math.min(targetIndex, castList.count - 1)
            castList.positionViewAtIndex(castList.currentIndex, ListView.Contain)
            ensureItemVisible(castSection, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
            return focusCurrentViewItem(castList)
        }

        if (area === "libraryRecommendations" && libraryRecommendationsSection.visible && libraryRecommendationsList.count > 0) {
            libraryRecommendationsList.currentIndex = Math.min(targetIndex, libraryRecommendationsList.count - 1)
            libraryRecommendationsList.positionViewAtIndex(libraryRecommendationsList.currentIndex, ListView.Contain)
            ensureItemVisible(libraryRecommendationsSection, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
            return focusCurrentViewItem(libraryRecommendationsList)
        }

        if (area === "seerrRecommendations" && seerrRecommendationsSection.visible && seerrRecommendationsList.count > 0) {
            seerrRecommendationsList.currentIndex = Math.min(targetIndex, seerrRecommendationsList.count - 1)
            seerrRecommendationsList.positionViewAtIndex(seerrRecommendationsList.currentIndex, ListView.Contain)
            ensureItemVisible(seerrRecommendationsSection, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
            return focusCurrentViewItem(seerrRecommendationsList)
        }

        return false
    }

    function restoreReturnState(state) {
        if (!state || String(state.seriesId || "") !== String(seriesId || "")) {
            return false
        }

        if (state.seerrRecommendations && state.seerrRecommendations.length > 0) {
            seerrRecommendedItems = state.seerrRecommendations.slice()
            seerrLoadedTmdbId = String(state.seerrTmdbId || "")
            seerrPendingTmdbId = ""
            seerrRecommendationsLoading = false
        }

        if (state.contentY !== undefined && state.contentY >= 0) {
            const maxScroll = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
            contentFlickable.contentY = Math.min(state.contentY, maxScroll)
        }

        const desiredArea = state.focusArea || "hero"
        if (desiredArea === "seasons" && seasonsGrid.count <= 0) return false
        if (desiredArea === "cast" && castList.count <= 0) return false
        if (desiredArea === "libraryRecommendations" && libraryRecommendations.length <= 0) return false
        if (desiredArea === "seerrRecommendations" && seerrRecommendedItems.length <= 0) return false

        if (restoreFocusToArea(desiredArea, state.focusIndex || 0)) {
            return true
        }

        if (restoreFocusToArea("seasons", state.seasonsGridIndex || 0)) {
            return true
        }

        if (playButton.enabled) {
            playButton.forceActiveFocus()
            return true
        }

        focusFirstLowerSection()
        return true
    }

    function tryRestorePendingReturnState() {
        if (!hasPendingReturnStateForCurrentSeries() || restoringFocusFromReturnState) {
            return
        }

        restoringFocusFromReturnState = true
        const restored = restoreReturnState(pendingReturnState)
        if (restored) {
            focusTimer.stop()
            suppressHeroAutofocus = true
            returnStateConsumed()
            heroAutofocusResetTimer.restart()
        }
        Qt.callLater(function() {
            restoringFocusFromReturnState = false
        })
    }

    function requestSeerrRecommendations() {
        const requestedTmdbId = String(tmdbId || "")

        if (!seerrConfigured || requestedTmdbId === "" || Number(requestedTmdbId) <= 0) {
            seerrPendingTmdbId = ""
            return
        }

        if (seerrRecommendationCacheStore) {
            const cachedEntry = seerrRecommendationCacheStore[requestedTmdbId]
            const now = Date.now()
            if (cachedEntry && cachedEntry.timestamp && (now - cachedEntry.timestamp) <= seerrRecommendationCacheTtlMs) {
                seerrRecommendedItems = (cachedEntry.items || []).slice()
                seerrLoadedTmdbId = requestedTmdbId
                seerrPendingTmdbId = ""
                seerrRecommendationsLoading = false
                return
            }
        }

        if ((seerrRecommendationsLoading && seerrPendingTmdbId === requestedTmdbId)
                || seerrLoadedTmdbId === requestedTmdbId) {
            return
        }

        seerrPendingTmdbId = ""
        seerrLoadedTmdbId = ""
        seerrRecommendedItems = []
        seerrRecommendationsLoading = false
        seerrPendingTmdbId = requestedTmdbId
        seerrRecommendationsLoading = true
        SeerrService.getSimilar(seerrMediaType, Number(requestedTmdbId), 1)
    }

    function restorePendingSeasonFocus() {
        if (root.pendingSeasonsGridIndex < 0 || seasonsGrid.count <= 0) {
            if (seasonsGrid.count > 0 && seasonsGrid.currentIndex < 0) {
                seasonsGrid.currentIndex = 0
            }
            return
        }

        const targetIndex = Math.min(root.pendingSeasonsGridIndex, seasonsGrid.count - 1)
        seasonsGrid.currentIndex = targetIndex
        seasonsGrid.positionViewAtIndex(targetIndex, GridView.Contain)
        seasonsGrid.forceActiveFocus()
        root.pendingSeasonsGridIndex = -1
        ensureItemVisible(seasonsGrid, Math.round(80 * Theme.layoutScale), Math.round(120 * Theme.layoutScale))
    }

    Keys.onPressed: (event) => {
        if (event.isAutoRepeat) {
            event.accepted = true
            return
        }

        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            if (contextMenu.opened) {
                event.accepted = true
                return
            }
            root.backRequested()
            event.accepted = true
        }
    }

    onActiveFocusChanged: {
        if (!activeFocus) {
            return
        }
        if ((restoringFocusFromSidebar && savedFocusItem)
                || restoringFocusFromReturnState
                || suppressHeroAutofocus
                || hasPendingReturnStateForCurrentSeries()) {
            return
        }
        Qt.callLater(function() {
            if (playButton && playButton.enabled) {
                playButton.forceActiveFocus()
            } else {
                focusFirstLowerSection()
            }
        })
    }

    onSeriesIdChanged: {
        if (hasPendingReturnStateForCurrentSeries()
                && pendingReturnState.seerrRecommendations
                && pendingReturnState.seerrRecommendations.length > 0) {
            seerrRecommendedItems = pendingReturnState.seerrRecommendations.slice()
            seerrRecommendationsLoading = false
            seerrPendingTmdbId = ""
            seerrLoadedTmdbId = String(pendingReturnState.seerrTmdbId || "")
        } else {
            seerrRecommendedItems = []
            seerrRecommendationsLoading = false
            seerrPendingTmdbId = ""
            seerrLoadedTmdbId = ""
        }

        if (seriesId !== "") {
            SeriesDetailsViewModel.loadSeriesDetails(seriesId)
        } else {
            SeriesDetailsViewModel.clear()
        }
    }

    onPendingReturnStateChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onRestorePendingReturnStateChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onCastAndCrewChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onLibraryRecommendationsChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onSeerrRecommendedItemsChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onPendingSeasonsGridIndexChanged: {
        if (pendingSeasonsGridIndex >= 0) {
            Qt.callLater(root.restorePendingSeasonFocus)
        }
    }

    // Full-bleed backdrop: sharp artwork under the floating rail, with a
    // leading-edge scrim for the hero copy and a floor for the shelves.
    Item {
        anchors.fill: parent
        anchors.leftMargin: -Theme.navRailReservedWidth
        z: -1
        clip: true

        Rectangle {
            anchors.fill: parent
            color: Theme.backgroundPrimary
        }

        Image {
            anchors.fill: parent
            source: backdropUrl !== "" ? backdropUrl : posterUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }

            // Poster fallbacks are low resolution; soften them.
            layer.enabled: backdropUrl === ""
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 0.8
                blurMax: 48
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.90) }
                GradientStop { position: 0.45; color: Theme.withAlpha(Theme.backgroundPrimary, 0.55) }
                GradientStop { position: 0.85; color: Theme.withAlpha(Theme.backgroundPrimary, 0.10) }
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
                GradientStop { position: 0.45; color: Theme.withAlpha(Theme.backgroundPrimary, 0.25) }
                GradientStop { position: 0.75; color: Theme.withAlpha(Theme.backgroundPrimary, 0.82) }
                GradientStop { position: 1.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.96) }
            }
        }

        // Quiet the artwork as the page scrolls into the shelves.
        Rectangle {
            anchors.fill: parent
            color: Theme.backgroundPrimary
            opacity: Math.min(0.75, contentFlickable.contentY / Math.max(1, heroPanel.height) * 0.75)
        }
    }

    StateMessage {
        anchors.centerIn: parent
        visible: isLoading && seriesName === ""
        z: 100
        kind: "loading"
        message: qsTr("Loading series details…")
    }

    Flickable {
        id: contentFlickable
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMarginX
        anchors.rightMargin: Theme.pageMarginX
        anchors.topMargin: Theme.pageMarginTop
        contentWidth: width
        contentHeight: contentColumn.implicitHeight + Math.round(180 * Theme.layoutScale)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Behavior on contentY {
            NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }

        ColumnLayout {
            id: contentColumn
            width: contentFlickable.width
            spacing: Theme.sectionSpacing

            GlassPanel {
                id: heroPanel
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(heroPosterHeight + root.heroPanelPadding * 2,
                                                 heroContent.implicitHeight + root.heroPanelPadding * 2)

                RowLayout {
                    id: heroContent
                    anchors.fill: parent
                    anchors.margins: root.heroPanelPadding
                    spacing: Theme.spacingXLarge + Theme.spacingSmall

                    Item {
                        Layout.preferredWidth: heroPosterWidth
                        Layout.preferredHeight: heroPosterHeight
                        Layout.alignment: Qt.AlignTop

                        ArtworkPlaceholder {
                            anchors.fill: parent
                            visible: heroPosterImage.status !== Image.Ready
                            loading: heroPosterImage.status === Image.Loading
                            iconGlyph: Icons.tvShows
                            title: seriesName
                        }

                        Image {
                            id: heroPosterImage
                            anchors.fill: parent
                            source: posterUrl
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            visible: status === Image.Ready

                            layer.enabled: true
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: posterMask
                            }
                        }

                        Rectangle {
                            id: posterMask
                            anchors.fill: parent
                            radius: Theme.imageRadius
                            visible: false
                            layer.enabled: true
                            layer.smooth: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Theme.spacingMedium

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: logoUrl !== "" ? Theme.detailViewLogoHeight : titleFallback.implicitHeight

                            ArtworkLogo {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(Theme.seriesLogoMaxWidth, parent.width)
                                height: parent.height
                                source: logoUrl
                                fillMode: Image.PreserveAspectFit
                                horizontalAlignment: Image.AlignLeft
                                asynchronous: true
                                cache: true
                                visible: logoUrl !== ""
                                opacity: status === Image.Ready ? 1.0 : 0.0
                                Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
                            }

                            Text {
                                id: titleFallback
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                text: seriesName
                                visible: logoUrl === ""
                                font.pixelSize: Theme.fontSizeHero
                                font.family: Theme.fontPrimary
                                font.weight: Theme.fontWeightHeavy
                                color: Theme.textPrimary
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                lineHeight: 0.95
                            }
                        }

                        MetadataLine {
                            Layout.fillWidth: true
                            font.pixelSize: Theme.fontSizeBody
                            items: [root.formatSeriesYears(), SeriesDetailsViewModel.officialRating,
                                    SeriesDetailsViewModel.status, root.formatCounts()]
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium
                            visible: ratingsRepeater.count > 0

                            Repeater {
                                id: ratingsRepeater
                                model: root.visibleExternalRatings()

                                RatingMetadataChip {
                                    required property var modelData
                                    ratingData: modelData
                                }
                            }
                        }

                        MetadataLine {
                            Layout.fillWidth: true
                            separator: "  ·  "
                            color: Theme.textTertiary
                            items: SeriesDetailsViewModel.genres || []
                        }

                        Text {
                            text: root.creatorsSummary() !== ""
                                  ? qsTr("Key creatives: %1").arg(root.creatorsSummary())
                                  : ""
                            Layout.fillWidth: true
                            visible: text !== ""
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Item {
                            id: overviewContainer
                            Layout.fillWidth: true
                            Layout.preferredHeight: overviewColumn.implicitHeight
                            visible: seriesOverview !== ""

                            property bool expanded: false
                            readonly property int collapsedLines: 4
                            property bool hasOverflow: overviewText.truncated || expanded

                            ColumnLayout {
                                id: overviewColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                spacing: Theme.spacingSmall

                                Text {
                                    id: overviewText
                                    Layout.fillWidth: true
                                    text: seriesOverview
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: Theme.textPrimary
                                    opacity: 0.9
                                    lineHeight: 1.12
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: overviewContainer.expanded ? 1000 : overviewContainer.collapsedLines
                                    elide: Text.ElideRight
                                }

                                ActionButton {
                                    id: readMoreButton
                                    variant: "ghost"
                                    visible: overviewContainer.hasOverflow
                                    Layout.alignment: Qt.AlignLeft
                                    Layout.leftMargin: -Theme.spacingMedium
                                    implicitHeight: Theme.buttonHeightSmall
                                    sidePadding: Theme.spacingMedium
                                    labelPixelSize: Theme.fontSizeSmall
                                    text: overviewContainer.expanded ? qsTr("Show Less") : qsTr("Read More")
                                    iconGlyph: overviewContainer.expanded ? Icons.expandLess : Icons.expandMore
                                    onClicked: overviewContainer.expanded = !overviewContainer.expanded
                                    Keys.onReturnPressed: (event) => { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                                    Keys.onEnterPressed: (event) => { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                                    Keys.onDownPressed: (event) => { playButton.forceActiveFocus(); event.accepted = true }
                                    Keys.onUpPressed: (event) => { event.accepted = true }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingSmall
                            spacing: Theme.controlSpacing

                            ActionButton {
                                id: playButton
                                variant: "primary"
                                readonly property string episodeCode: hasNextEpisode && SeriesDetailsViewModel.nextSeasonNumber >= 0
                                                                      && SeriesDetailsViewModel.nextEpisodeNumber >= 0
                                                                      ? " S" + SeriesDetailsViewModel.nextSeasonNumber
                                                                        + " E" + SeriesDetailsViewModel.nextEpisodeNumber
                                                                      : ""
                                text: hasNextEpisode
                                      ? ((SeriesDetailsViewModel.nextEpisodePositionMs > 0 ? qsTr("Resume") : qsTr("Play")) + episodeCode)
                                      : qsTr("Start Series")
                                iconGlyph: Icons.playArrow
                                enabled: seriesId !== ""
                                implicitHeight: Theme.buttonHeightLarge
                                Layout.minimumWidth: Math.round(280 * Theme.layoutScale)

                                KeyNavigation.right: favoriteButton

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        root.ensureTopVisible()
                                    }
                                }

                                Keys.onDownPressed: (event) => {
                                    root.focusFirstLowerSection()
                                    event.accepted = true
                                }

                                Keys.onUpPressed: (event) => {
                                    if (readMoreButton.visible)
                                        readMoreButton.forceActiveFocus()
                                    event.accepted = true
                                }

                                Keys.onReturnPressed: (event) => {
                                    if (!event.isAutoRepeat && enabled) {
                                        clicked()
                                        event.accepted = true
                                    }
                                }

                                Keys.onEnterPressed: (event) => {
                                    if (!event.isAutoRepeat && enabled) {
                                        clicked()
                                        event.accepted = true
                                    }
                                }

                                onClicked: {
                                    if (hasNextEpisode) {
                                        root.playNextEpisode(SeriesDetailsViewModel.nextEpisodeId,
                                                             SeriesDetailsViewModel.nextEpisodePositionMs)
                                        return
                                    }

                                    let firstRealSeasonIndex = -1
                                    const count = SeriesDetailsViewModel.seasonsModel.rowCount()
                                    for (let i = 0; i < count; ++i) {
                                        const season = SeriesDetailsViewModel.seasonsModel.getItem(i)
                                        if (season && season.indexNumber >= 1) {
                                            firstRealSeasonIndex = i
                                            break
                                        }
                                    }
                                    root.navigateToSeasons(firstRealSeasonIndex >= 0 ? firstRealSeasonIndex : 0)
                                }
                            }

                            SecondaryActionButton {
                                id: favoriteButton
                                text: isFavorite ? qsTr("Favorited") : qsTr("Favorite")
                                iconGlyph: isFavorite ? Icons.favorite : Icons.favoriteBorder
                                iconColor: isFavorite ? Theme.favoriteColor : Theme.textPrimary
                                implicitHeight: Theme.buttonHeightLarge

                                KeyNavigation.left: playButton
                                KeyNavigation.right: contextMenuButton

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        root.ensureTopVisible()
                                    }
                                }

                                Keys.onDownPressed: (event) => {
                                    root.focusFirstLowerSection()
                                    event.accepted = true
                                }

                                Keys.onReturnPressed: (event) => {
                                    if (!event.isAutoRepeat) {
                                        clicked()
                                        event.accepted = true
                                    }
                                }

                                Keys.onEnterPressed: (event) => {
                                    if (!event.isAutoRepeat) {
                                        clicked()
                                        event.accepted = true
                                    }
                                }

                                onClicked: SeriesDetailsViewModel.toggleFavorite()
                            }

                            SecondaryActionButton {
                                id: contextMenuButton
                                text: ""
                                accessibleLabel: qsTr("More options")
                                iconGlyph: Icons.moreHoriz
                                implicitHeight: Theme.buttonHeightLarge

                                KeyNavigation.left: favoriteButton

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        root.ensureTopVisible()
                                    }
                                }

                                Keys.onDownPressed: (event) => {
                                    root.focusFirstLowerSection()
                                    event.accepted = true
                                }

                                Keys.onReturnPressed: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height + Theme.spacingSmall)
                                Keys.onEnterPressed: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height + Theme.spacingSmall)
                                onClicked: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height + Theme.spacingSmall)
                            }

                            Item { Layout.fillWidth: true }
                        }

                        // The episode the primary action will play.
                        RowLayout {
                            Layout.fillWidth: true
                            visible: hasNextEpisode && SeriesDetailsViewModel.nextEpisodeName !== ""
                            spacing: Theme.spacingSmall

                            Text {
                                text: qsTr("Up Next")
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontPrimary
                                font.weight: Theme.fontWeightHeavy
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: Math.round(1.5 * Theme.layoutScale)
                                color: Theme.accentPrimary
                            }

                            Text {
                                Layout.fillWidth: true
                                text: SeriesDetailsViewModel.nextEpisodeName
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                id: seasonsSection
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Text {
                    text: qsTr("Seasons")
                    font.pixelSize: Theme.fontSizeHeader
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightHeavy
                    color: Theme.textPrimary
                }

                ColumnLayout {
                    visible: isLoading && seasonsGrid.count === 0
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    Repeater {
                        model: 3
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.round(170 * Theme.layoutScale)
                            radius: Theme.radiusMedium
                            color: Theme.surfaceInteractive
                        }
                    }
                }

                GridView {
                    id: seasonsGrid
                    Layout.fillWidth: true
                    property int tileWidth: root.recommendationCardWidth
                    property int tileGap: Math.round(22 * Theme.layoutScale)
                    property int idealColumns: Math.max(1, Math.min(6, Math.floor((width + tileGap) / (tileWidth + tileGap))))
                    property int columns: count > 0 ? Math.min(count, idealColumns) : idealColumns
                    property real rawCellHeight: root.recommendationCardHeight

                    cellWidth: tileWidth + tileGap
                    cellHeight: rawCellHeight
                    Layout.preferredHeight: {
                        if (count === 0) return cellHeight
                        const cols = Math.max(1, columns)
                        const rows = Math.ceil(count / cols)
                        return rows * cellHeight + Math.round(36 * Theme.layoutScale)
                    }
                    clip: false
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    focus: false
                    topMargin: Math.round(10 * Theme.layoutScale)
                    bottomMargin: Math.round(10 * Theme.layoutScale)
                    leftMargin: Math.round(6 * Theme.layoutScale)
                    rightMargin: Math.round(6 * Theme.layoutScale)
                    model: SeriesDetailsViewModel.seasonsModel

                    function focusCurrentOrFirst() {
                        if (count <= 0) {
                            return
                        }
                        currentIndex = Math.max(0, currentIndex)
                        forceActiveFocus()
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root.ensureItemVisible(seasonsGrid, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
                        }
                    }

                    onCurrentIndexChanged: {
                        if (activeFocus && currentIndex >= 0) {
                            positionViewAtIndex(currentIndex, GridView.Contain)
                            root.ensureItemVisible(seasonsGrid, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
                            SeriesDetailsViewModel.prefetchSeasonsAround(currentIndex, 2)
                        }
                    }

                    Keys.onUpPressed: (event) => {
                        const previousRowIndex = currentIndex - columns
                        if (previousRowIndex >= 0) {
                            currentIndex = previousRowIndex
                        } else {
                            playButton.forceActiveFocus()
                        }
                        event.accepted = true
                    }

                    Keys.onDownPressed: (event) => {
                        const nextRowIndex = currentIndex + columns
                        if (nextRowIndex < count) {
                            currentIndex = nextRowIndex
                        } else {
                            root.focusTarget(root.nextSectionAfterSeasons())
                        }
                        event.accepted = true
                    }

                    Keys.onLeftPressed: (event) => {
                        const currentColumn = currentIndex % columns
                        if (currentColumn > 0) {
                            currentIndex = Math.max(0, currentIndex - 1)
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }

                    Keys.onRightPressed: (event) => {
                        const nextIndex = currentIndex + 1
                        const currentColumn = currentIndex % columns
                        const isLastColumn = currentColumn === columns - 1
                        if (!isLastColumn && nextIndex < count) {
                            currentIndex = nextIndex
                        }
                        event.accepted = true
                    }

                    Keys.onReturnPressed: {
                        if (currentIndex >= 0 && currentIndex < count) {
                            root.navigateToSeasons(currentIndex)
                        }
                    }

                    Keys.onEnterPressed: {
                        if (currentIndex >= 0 && currentIndex < count) {
                            root.navigateToSeasons(currentIndex)
                        }
                    }

                    delegate: Item {
                        id: seasonDelegate

                        required property int index
                        required property string name
                        required property string imageUrl
                        required property string itemId
                        required property int episodeCount
                        required property int unplayedItemCount
                        required property bool isPlayed

                        width: seasonsGrid.tileWidth
                        height: seasonsGrid.cellHeight

                        property real posterWidth: root.recommendationCardWidth
                        property real posterHeight: Math.round(posterWidth * 1.5)
                        property bool isFocused: seasonsGrid.currentIndex === index && seasonsGrid.activeFocus
                        property bool isHovered: InputModeManager.pointerActive && seasonMouseArea.containsMouse

                        scale: isFocused ? 1.035 : (isHovered ? 1.015 : 1.0)
                        z: isFocused ? 2 : 0
                        transformOrigin: Item.Center
                        Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

                        Column {
                            anchors.fill: parent
                            spacing: Theme.spacingSmall

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: seasonDelegate.posterWidth
                                height: seasonDelegate.posterHeight
                                radius: Theme.imageRadius
                                color: "transparent"
                                clip: false

                                Image {
                                    id: seasonArt
                                    anchors.fill: parent
                                    source: seasonDelegate.imageUrl !== "" ? seasonDelegate.imageUrl : posterUrl
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: true

                                    layer.enabled: true
                                    layer.effect: MultiEffect {
                                        maskEnabled: true
                                        maskSource: seasonMask
                                    }
                                }

                                Item {
                                    id: seasonMask
                                    anchors.fill: parent
                                    visible: false
                                    layer.enabled: true
                                    layer.smooth: true

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: seasonArt.paintedWidth
                                        height: seasonArt.paintedHeight
                                        radius: Theme.imageRadius
                                        color: "white"
                                    }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.imageRadius
                                    color: Theme.artworkPlaceholder
                                    visible: seasonArt.status !== Image.Ready

                                    Text {
                                        anchors.centerIn: parent
                                        text: "..."
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeBody
                                        font.family: Theme.fontPrimary
                                    }
                                }

                                Item {
                                    width: seasonArt.paintedWidth
                                    height: seasonArt.paintedHeight
                                    anchors.centerIn: parent

                                    FocusRing {
                                        active: seasonDelegate.isFocused
                                        radius: Theme.imageRadius
                                    }

                                    UnwatchedBadge {
                                        anchors.top: parent.top
                                        anchors.right: parent.right
                                        parentWidth: parent.width
                                        count: seasonDelegate.unplayedItemCount
                                        isFullyWatched: seasonDelegate.isPlayed
                                    }
                                }
                            }

                            Item {
                                width: seasonDelegate.posterWidth
                                height: seasonNameLabel.implicitHeight
                                anchors.horizontalCenter: parent.horizontalCenter

                                ScrollingCardLabel {
                                    id: seasonNameLabel
                                    anchors.fill: parent
                                    text: seasonDelegate.name
                                    fontPixelSize: Theme.fontSizeSmall
                                    fontWeight: Font.DemiBold
                                    textColor: Theme.textPrimary
                                    active: seasonDelegate.isFocused
                                }
                            }

                            Item {
                                width: seasonDelegate.posterWidth
                                height: seasonEpisodesLabel.implicitHeight
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: seasonEpisodesLabel.text !== ""

                                ScrollingCardLabel {
                                    id: seasonEpisodesLabel
                                    anchors.fill: parent
                                    text: seasonDelegate.episodeCount > 0 ? seasonDelegate.episodeCount + qsTr(" Episodes") : ""
                                    fontPixelSize: Theme.fontSizeSmall
                                    fontWeight: Font.Normal
                                    textColor: Theme.textSecondary
                                    active: seasonDelegate.isFocused
                                }
                            }
                        }

                        MouseArea {
                            id: seasonMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                seasonsGrid.currentIndex = seasonDelegate.index
                                seasonsGrid.forceActiveFocus()
                                root.navigateToSeasons(seasonDelegate.index)
                            }
                        }
                    }
                }
            }

            FocusScope {
                id: castSection
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: castAndCrew.length > 0
                implicitHeight: castSectionContent.implicitHeight

                function focusCurrentOrFirst() {
                    if (castList.count <= 0) {
                        return
                    }
                    castList.currentIndex = Math.max(0, castList.currentIndex)
                    castList.forceActiveFocus()
                }

                ColumnLayout {
                    id: castSectionContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: Theme.spacingMedium

                    Text {
                        text: qsTr("Cast & Crew")
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }

                    ListView {
                        id: castList
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.peopleCardHeight + Math.round(16 * Theme.layoutScale)
                        orientation: ListView.Horizontal
                        spacing: Theme.spacingMedium
                        model: castAndCrew
                        clip: false
                        interactive: false
                        boundsBehavior: Flickable.StopAtBounds
                        leftMargin: Math.round(8 * Theme.layoutScale)
                        rightMargin: Math.round(8 * Theme.layoutScale)
                        topMargin: Math.round(8 * Theme.layoutScale)
                        bottomMargin: Math.round(8 * Theme.layoutScale)

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureItemVisible(castSection, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
                            }
                        }

                        onCurrentIndexChanged: {
                            if (activeFocus && currentIndex >= 0) {
                                positionViewAtIndex(currentIndex, ListView.Contain)
                            }
                        }

                        delegate: FocusScope {
                            id: castDelegate

                            required property int index
                            required property var modelData

                            width: root.peopleCardWidth
                            height: root.peopleCardHeight

                            Keys.onLeftPressed: (event) => {
                                if (index > 0) {
                                    castList.currentIndex = index - 1
                                    event.accepted = true
                                } else {
                                    event.accepted = false
                                }
                            }

                            Keys.onRightPressed: {
                                if (index + 1 < castList.count) {
                                    castList.currentIndex = index + 1
                                }
                            }

                            Keys.onUpPressed: {
                                seasonsGrid.focusCurrentOrFirst()
                            }

                            Keys.onDownPressed: {
                                root.focusTarget(root.nextSectionAfterCast())
                            }

                            Keys.onReturnPressed: (event) => {
                                event.accepted = true
                            }

                            Keys.onEnterPressed: (event) => {
                                event.accepted = true
                            }

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    castList.currentIndex = index
                                }
                            }

                            PersonCard {
                                anchors.fill: parent
                                itemData: modelData
                                isFocused: castList.activeFocus && castList.currentIndex === index
                            }
                        }

                        WheelStepScroller {
                            anchors.fill: parent
                            target: castList
                            orientation: Qt.Horizontal
                            stepPx: root.peopleCardWidth + Theme.spacingMedium
                        }
                    }
                }
            }

            FocusScope {
                id: libraryRecommendationsSection
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: libraryRecommendationsLoading || libraryRecommendations.length > 0
                implicitHeight: libraryRecommendationsContent.implicitHeight

                function focusCurrentOrFirst() {
                    if (libraryRecommendationsList.count <= 0) {
                        root.focusTarget(root.nextSectionAfterLibraryRecommendations())
                        return
                    }
                    libraryRecommendationsList.currentIndex = Math.max(0, libraryRecommendationsList.currentIndex)
                    libraryRecommendationsList.forceActiveFocus()
                }

                ColumnLayout {
                    id: libraryRecommendationsContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: Theme.spacingMedium

                    Text {
                        text: qsTr("Recommended From Your Library")
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(120 * Theme.layoutScale)
                        visible: libraryRecommendationsLoading && libraryRecommendations.length === 0
                        radius: Theme.radiusMedium
                        color: Theme.surfaceInteractive
                        border.width: 1
                        border.color: Theme.cardBorder

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: parent.visible
                        }
                    }

                    ListView {
                        id: libraryRecommendationsList
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.recommendationCardHeight + Math.round(16 * Theme.layoutScale)
                        visible: libraryRecommendations.length > 0
                        orientation: ListView.Horizontal
                        spacing: Theme.spacingMedium
                        model: libraryRecommendations
                        clip: false
                        interactive: false
                        boundsBehavior: Flickable.StopAtBounds
                        leftMargin: root.shelfEdgePadding
                        rightMargin: root.shelfEdgePadding
                        topMargin: Math.round(8 * Theme.layoutScale)
                        bottomMargin: Math.round(8 * Theme.layoutScale)

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureItemVisible(libraryRecommendationsSection, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
                            }
                        }

                        onCurrentIndexChanged: {
                            if (activeFocus && currentIndex >= 0) {
                                positionViewAtIndex(currentIndex, ListView.Contain)
                            }
                        }

                        delegate: FocusScope {
                            id: libraryDelegate

                            required property int index
                            required property var modelData

                            width: root.recommendationCardWidth
                            height: root.recommendationCardHeight

                            Keys.onLeftPressed: (event) => {
                                if (index > 0) {
                                    libraryRecommendationsList.currentIndex = index - 1
                                    event.accepted = true
                                } else {
                                    event.accepted = false
                                }
                            }

                            Keys.onRightPressed: {
                                if (index + 1 < libraryRecommendationsList.count) {
                                    libraryRecommendationsList.currentIndex = index + 1
                                }
                            }

                            Keys.onUpPressed: {
                                if (castSection.visible) {
                                    castSection.focusCurrentOrFirst()
                                } else {
                                    seasonsGrid.focusCurrentOrFirst()
                                }
                            }

                            Keys.onDownPressed: {
                                root.focusTarget(root.nextSectionAfterLibraryRecommendations())
                            }

                            Keys.onReturnPressed: {
                                root.itemSelected(modelData)
                            }

                            Keys.onEnterPressed: {
                                root.itemSelected(modelData)
                            }

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    libraryRecommendationsList.currentIndex = index
                                }
                            }

                            RecommendationPosterCard {
                                anchors.fill: parent
                                itemData: modelData
                                isFocused: libraryRecommendationsList.activeFocus && libraryRecommendationsList.currentIndex === index
                                onClicked: {
                                    libraryDelegate.forceActiveFocus()
                                    libraryRecommendationsList.currentIndex = index
                                    root.itemSelected(modelData)
                                }
                            }
                        }

                        WheelStepScroller {
                            anchors.fill: parent
                            target: libraryRecommendationsList
                            orientation: Qt.Horizontal
                            stepPx: root.recommendationCardWidth + Theme.spacingMedium
                        }
                    }
                }
            }

            FocusScope {
                id: seerrRecommendationsSection
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: seerrRecommendationsLoading || seerrRecommendedItems.length > 0
                implicitHeight: seerrRecommendationsContent.implicitHeight

                function focusCurrentOrFirst() {
                    if (seerrRecommendationsList.count <= 0) {
                        root.focusTarget(root.nextSectionAfterSeerrRecommendations())
                        return
                    }
                    seerrRecommendationsList.currentIndex = Math.max(0, seerrRecommendationsList.currentIndex)
                    seerrRecommendationsList.forceActiveFocus()
                }

                ColumnLayout {
                    id: seerrRecommendationsContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: Theme.spacingMedium

                    Text {
                        text: qsTr("Seerr Recommendations")
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(120 * Theme.layoutScale)
                        visible: seerrRecommendationsLoading && seerrRecommendedItems.length === 0
                        radius: Theme.radiusMedium
                        color: Theme.surfaceInteractive
                        border.width: 1
                        border.color: Theme.cardBorder

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: parent.visible
                        }
                    }

                    ListView {
                        id: seerrRecommendationsList
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.recommendationCardHeight + Math.round(16 * Theme.layoutScale)
                        visible: seerrRecommendedItems.length > 0
                        orientation: ListView.Horizontal
                        spacing: Theme.spacingMedium
                        model: seerrRecommendedItems
                        clip: false
                        interactive: false
                        boundsBehavior: Flickable.StopAtBounds
                        leftMargin: root.shelfEdgePadding
                        rightMargin: root.shelfEdgePadding
                        topMargin: Math.round(8 * Theme.layoutScale)
                        bottomMargin: Math.round(8 * Theme.layoutScale)

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureItemVisible(seerrRecommendationsSection, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
                            }
                        }

                        onCurrentIndexChanged: {
                            if (activeFocus && currentIndex >= 0) {
                                positionViewAtIndex(currentIndex, ListView.Contain)
                            }
                        }

                        delegate: FocusScope {
                            id: seerrDelegate

                            required property int index
                            required property var modelData

                            width: root.recommendationCardWidth
                            height: root.recommendationCardHeight

                            Keys.onLeftPressed: (event) => {
                                if (index > 0) {
                                    seerrRecommendationsList.currentIndex = index - 1
                                    event.accepted = true
                                } else {
                                    event.accepted = false
                                }
                            }

                            Keys.onRightPressed: {
                                if (index + 1 < seerrRecommendationsList.count) {
                                    seerrRecommendationsList.currentIndex = index + 1
                                }
                            }

                            Keys.onUpPressed: {
                                if (libraryRecommendationsSection.visible) {
                                    libraryRecommendationsSection.focusCurrentOrFirst()
                                } else if (castSection.visible) {
                                    castSection.focusCurrentOrFirst()
                                } else {
                                    seasonsGrid.focusCurrentOrFirst()
                                }
                            }

                            Keys.onDownPressed: (event) => {
                                event.accepted = true
                            }

                            Keys.onReturnPressed: {
                                seerrRequestDialog.openForItem(modelData, seerrDelegate)
                            }

                            Keys.onEnterPressed: {
                                seerrRequestDialog.openForItem(modelData, seerrDelegate)
                            }

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    seerrRecommendationsList.currentIndex = index
                                }
                            }

                            RecommendationPosterCard {
                                anchors.fill: parent
                                itemData: modelData
                                isFocused: seerrRecommendationsList.activeFocus && seerrRecommendationsList.currentIndex === index
                                onClicked: {
                                    seerrDelegate.forceActiveFocus()
                                    seerrRecommendationsList.currentIndex = index
                                    seerrRequestDialog.openForItem(modelData, seerrDelegate)
                                }
                            }
                        }

                        WheelStepScroller {
                            anchors.fill: parent
                            target: seerrRecommendationsList
                            orientation: Qt.Horizontal
                            stepPx: root.recommendationCardWidth + Theme.spacingMedium
                        }
                    }
                }
            }
        }
    }

    WheelStepScroller {
        anchors.fill: contentFlickable
        target: contentFlickable
        stepPx: Math.round(140 * Theme.layoutScale)
    }

    SeerrRequestDialog {
        id: seerrRequestDialog
        parent: Overlay.overlay
    }

    Connections {
        target: SeriesDetailsViewModel

        function onSeriesLoaded() {
            if (!hasPendingReturnStateForCurrentSeries() && !suppressHeroAutofocus) {
                focusTimer.start()
            }
            Qt.callLater(root.requestSeerrRecommendations)
            Qt.callLater(root.tryRestorePendingReturnState)
        }
    }

    Connections {
        target: SeriesDetailsViewModel.seasonsModel

        function onModelReset() {
            Qt.callLater(root.restorePendingSeasonFocus)
            Qt.callLater(root.tryRestorePendingReturnState)
        }
    }

    Connections {
        target: SeerrService

        function onConfiguredChanged() {
            if (!SeerrService.configured) {
                seerrPendingTmdbId = ""
                seerrLoadedTmdbId = ""
                seerrRecommendedItems = []
                seerrRecommendationsLoading = false
                return
            }

            Qt.callLater(root.requestSeerrRecommendations)
        }

        function onSimilarResultsLoaded(mediaType, requestedTmdbId, results) {
            const requestTmdbId = String(requestedTmdbId)
            if (String(mediaType).toLowerCase() !== seerrMediaType
                    || requestTmdbId !== root.tmdbId
                    || requestTmdbId !== seerrPendingTmdbId) {
                return
            }

            const mappedResults = []
            for (let i = 0; i < results.length; ++i) {
                mappedResults.push(results[i])
            }
            seerrRecommendedItems = mappedResults
            if (root.seerrRecommendationCacheStore) {
                root.seerrRecommendationCacheStore[requestTmdbId] = {
                    timestamp: Date.now(),
                    items: mappedResults.slice()
                }
            }
            seerrLoadedTmdbId = requestTmdbId
            seerrPendingTmdbId = ""
            seerrRecommendationsLoading = false
            Qt.callLater(root.tryRestorePendingReturnState)
        }

        function onSimilarResultsFailed(mediaType, requestedTmdbId, error) {
            const requestTmdbId = String(requestedTmdbId)
            if (String(mediaType).toLowerCase() !== seerrMediaType
                    || requestTmdbId !== root.tmdbId
                    || requestTmdbId !== seerrPendingTmdbId) {
                return
            }

            console.warn("[SeriesDetailsView] Seerr similar titles failed:", error)
            seerrPendingTmdbId = ""
            seerrRecommendationsLoading = false
        }
    }

    Timer {
        id: focusTimer
        interval: 50
        repeat: false

        onTriggered: {
            if (root.suppressHeroAutofocus) {
                return
            }
            if (playButton.enabled) {
                playButton.forceActiveFocus()
            } else {
                root.focusFirstLowerSection()
            }
        }
    }

    Timer {
        id: heroAutofocusResetTimer
        interval: 200
        repeat: false

        onTriggered: {
            root.suppressHeroAutofocus = false
        }
    }

    Component.onCompleted: {
        if (SeriesDetailsViewModel.seriesId !== "" && SeriesDetailsViewModel.title !== "") {
            if (!hasPendingReturnStateForCurrentSeries() && !suppressHeroAutofocus) {
                focusTimer.start()
            }
            Qt.callLater(root.restorePendingSeasonFocus)
            Qt.callLater(root.requestSeerrRecommendations)
            Qt.callLater(root.tryRestorePendingReturnState)
        }
    }

    property var savedFocusItem: null
    property int savedSeasonIndex: -1
    property bool restoringFocusFromSidebar: false
    property bool restoringFocusFromReturnState: false
    property bool suppressHeroAutofocus: false

    function saveFocusForSidebar() {
        savedSeasonIndex = seasonsGrid.currentIndex
        savedFocusItem = root.Window.activeFocusItem
    }

    function restoreFocusFromSidebar() {
        restoringFocusFromSidebar = true
        Qt.callLater(root.restoreSavedSidebarFocus)
    }

    function restoreSavedSidebarFocus() {
        if (savedSeasonIndex >= 0 && seasonsGrid.count > 0) {
            seasonsGrid.currentIndex = Math.min(savedSeasonIndex, seasonsGrid.count - 1)
            seasonsGrid.positionViewAtIndex(seasonsGrid.currentIndex, GridView.Contain)
        }

        if (savedFocusItem && savedFocusItem.parent && typeof savedFocusItem.forceActiveFocus === "function") {
            savedFocusItem.forceActiveFocus()
        } else if (savedSeasonIndex >= 0 && seasonsGrid.count > 0) {
            seasonsGrid.forceActiveFocus()
        } else if (playButton.enabled) {
            playButton.forceActiveFocus()
        } else {
            root.focusFirstLowerSection()
        }

        savedFocusItem = null
        Qt.callLater(function() {
            restoringFocusFromSidebar = false
        })
    }

    BloomMenu {
        id: contextMenu



        onOpened: {
            currentIndex = 0
            forceActiveFocus()
        }

        onClosed: {
            Qt.callLater(function() {
                contextMenuButton.forceActiveFocus()
            })
        }

        BloomMenuItem {
            id: watchedMenuItem
            text: isWatched ? qsTr("Mark as Unwatched") : qsTr("Mark as Watched")



            onTriggered: {
                if (isWatched) {
                    SeriesDetailsViewModel.markAsUnwatched()
                } else {
                    SeriesDetailsViewModel.markAsWatched()
                }
                contextMenu.close()
            }
        }

        MenuSeparator {
            contentItem: Rectangle {
                implicitHeight: 1
                color: Theme.borderLight
            }
        }

        BloomMenu {
            id: profileSubmenu
            title: qsTr("MPV Profile")

            property string currentProfile: ConfigManager.getSeriesProfile(root.seriesId)

            Connections {
                target: ConfigManager

                function onSeriesProfilesChanged() {
                    profileSubmenu.currentProfile = ConfigManager.getSeriesProfile(root.seriesId)
                }
            }

            onAboutToShow: {
                currentProfile = ConfigManager.getSeriesProfile(root.seriesId)
            }


            BloomMenuItem {
                text: profileSubmenu.currentProfile === "" ? qsTr("Current: Use Default")
                                                           : qsTr("Current: %1").arg(profileSubmenu.currentProfile)
                enabled: false


            }

            MenuSeparator {
                contentItem: Rectangle {
                    implicitHeight: 1
                    color: Theme.borderLight
                }
            }

            BloomMenuItem {
                text: qsTr("Use Default")

                marked: profileSubmenu.currentProfile === ""
                onTriggered: {
                    ConfigManager.setSeriesProfile(root.seriesId, "")
                    profileSubmenu.close()
                    contextMenu.close()
                }
            }

            MenuSeparator {
                contentItem: Rectangle {
                    implicitHeight: 1
                    color: Theme.borderLight
                }
            }

            Repeater {
                model: ConfigManager.mpvProfileNames

                BloomMenuItem {
                    required property string modelData

                    text: modelData

                    marked: profileSubmenu.currentProfile === modelData
                    onTriggered: {
                        ConfigManager.setSeriesProfile(root.seriesId, modelData)
                        profileSubmenu.close()
                        contextMenu.close()
                    }
                }
            }
        }
    }
}
