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
    signal playNextEpisode(string episodeId, var startPositionTicks)
    signal navigateToEpisode(var episodeData)
    signal itemSelected(var itemData)
    signal backRequested()
    signal returnStateConsumed()

    focus: true

    component ScrollingCardLabel: Item {
        id: scrollingLabel

        property string text: ""
        property color textColor: Theme.textPrimary
        property int fontPixelSize: Theme.fontSizeSmall
        property string fontFamily: Theme.fontPrimary
        property int fontWeight: Font.DemiBold
        property bool active: false

        implicitHeight: label.implicitHeight
        clip: true

        readonly property real overflowWidth: Math.max(0, label.implicitWidth - width)

        states: [
            State {
                name: "static"
                when: !labelScrollAnimation.running

                AnchorChanges {
                    target: label
                    anchors.left: scrollingLabel.overflowWidth > 0 ? scrollingLabel.left : undefined
                    anchors.horizontalCenter: scrollingLabel.overflowWidth > 0 ? undefined : scrollingLabel.horizontalCenter
                }

                PropertyChanges {
                    target: label
                    x: 0
                }
            },
            State {
                name: "scrolling"
                when: labelScrollAnimation.running

                AnchorChanges {
                    target: label
                    anchors.left: undefined
                    anchors.horizontalCenter: undefined
                }

                PropertyChanges {
                    target: label
                    x: 0
                }
            }
        ]

        Text {
            id: label
            text: scrollingLabel.text
            font.pixelSize: scrollingLabel.fontPixelSize
            font.family: scrollingLabel.fontFamily
            font.weight: scrollingLabel.fontWeight
            color: scrollingLabel.textColor
            wrapMode: Text.NoWrap
        }

        SequentialAnimation {
            id: labelScrollAnimation
            running: scrollingLabel.active && scrollingLabel.overflowWidth > 0
            loops: Animation.Infinite

            PauseAnimation { duration: 1000 }
            NumberAnimation {
                target: label
                property: "x"
                to: -scrollingLabel.overflowWidth
                duration: Math.max(1200, scrollingLabel.overflowWidth * 20)
                easing.type: Easing.Linear
            }
            PauseAnimation { duration: 1000 }
            NumberAnimation {
                target: label
                property: "x"
                to: 0
                duration: Math.max(1200, scrollingLabel.overflowWidth * 20)
                easing.type: Easing.Linear
            }
        }
    }

    component PersonCard: Item {
        id: personCard

        required property var itemData
        property bool isFocused: false
        property bool isHovered: InputModeManager.pointerActive && personMouseArea.containsMouse
        readonly property int posterFrameWidth: width
        readonly property int posterFrameHeight: Math.round(posterFrameWidth * 1.5)

        width: root.peopleCardWidth
        height: root.peopleCardHeight
        scale: isFocused ? 1.04 : (isHovered ? 1.02 : 1.0)
        transformOrigin: Item.Center
        Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: personCard.posterFrameHeight
                radius: Theme.imageRadius
                color: "transparent"
                clip: false

                Image {
                    id: personImage
                    anchors.fill: parent
                    source: personCard.itemData.Id && personCard.itemData.PrimaryImageTag
                            ? LibraryService.getCachedImageUrlWithWidth(personCard.itemData.Id, "Primary", 360)
                            : ""
                    fillMode: Image.PreserveAspectCrop
                    horizontalAlignment: Image.AlignHCenter
                    verticalAlignment: Image.AlignBottom
                    asynchronous: true
                    cache: true

                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: personImageMask
                    }
                }

                Item {
                    id: personImageMask
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

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.imageRadius
                    color: Qt.rgba(0.08, 0.08, 0.08, 0.45)
                    visible: personImage.status !== Image.Ready

                    Text {
                        anchors.centerIn: parent
                        text: Icons.person
                        font.family: Theme.fontIcon
                        font.pixelSize: Math.round(56 * Theme.layoutScale)
                        color: Theme.textSecondary
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.imageRadius
                    color: "transparent"
                    border.width: personCard.isFocused ? Theme.buttonFocusBorderWidth : 0
                    border.color: Theme.accentPrimary
                    visible: border.width > 0
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: castNameLabel.implicitHeight

                ScrollingCardLabel {
                    id: castNameLabel
                    anchors.fill: parent
                    text: personCard.itemData.Name || ""
                    fontPixelSize: Theme.fontSizeSmall
                    fontWeight: Font.DemiBold
                    textColor: Theme.textPrimary
                    active: personCard.isFocused
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: castSubtitleLabel.implicitHeight
                visible: castSubtitleLabel.text !== ""

                ScrollingCardLabel {
                    id: castSubtitleLabel
                    anchors.fill: parent
                    text: personCard.itemData.Subtitle || ""
                    fontPixelSize: Theme.fontSizeSmall
                    fontWeight: Font.Normal
                    textColor: Theme.textSecondary
                    active: personCard.isFocused
                }
            }
        }

        MouseArea {
            id: personMouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.forceActiveFocus()
        }
    }

    component RecommendationPosterCard: Item {
        id: recommendationCard

        required property var itemData
        property bool isFocused: false
        property bool isHovered: InputModeManager.pointerActive && recommendationMouseArea.containsMouse
        readonly property bool isSeerr: itemData.Source === "Seerr"
        readonly property string posterSource: {
            if (isSeerr) {
                return itemData.imageUrl || ""
            }
            if (itemData.Id) {
                return LibraryService.getCachedImageUrlWithWidth(itemData.Id, "Primary", 420)
            }
            return ""
        }
        readonly property string title: itemData.Name || ""
        readonly property string subtitle: {
            if (itemData.ProductionYear) {
                return String(itemData.ProductionYear)
            }
            if (itemData.PremiereDate) {
                const date = new Date(itemData.PremiereDate)
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
                    color: Qt.rgba(0.08, 0.08, 0.08, 0.45)
                    visible: recommendationPosterImage.status !== Image.Ready

                    Text {
                        anchors.centerIn: parent
                        text: recommendationCard.isSeerr
                              ? (recommendationCard.itemData.SeerrMediaType === "tv" ? Icons.tvShows : Icons.movie)
                              : (recommendationCard.itemData.Type === "Series" ? Icons.tvShows : Icons.movie)
                        font.family: Theme.fontIcon
                        font.pixelSize: Math.round(56 * Theme.layoutScale)
                        color: Theme.textSecondary
                    }
                }

                Item {
                    width: recommendationPosterImage.paintedWidth
                    height: recommendationPosterImage.paintedHeight
                    anchors.centerIn: parent

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width + border.width * 2
                        height: parent.height + border.width * 2
                        radius: Theme.imageRadius + border.width
                        color: "transparent"
                        border.width: recommendationCard.isFocused ? Theme.buttonFocusBorderWidth : 0
                        border.color: Theme.accentPrimary
                        visible: border.width > 0
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
        return parts.join("  ")
    }

    function creatorsSummary() {
        const people = castAndCrew || []
        const names = []
        for (let i = 0; i < people.length; ++i) {
            const person = people[i]
            const type = String(person.Type || "").toLowerCase()
            if (type === "director" || type === "writer" || type === "producer" || type === "creator") {
                names.push(person.Name)
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

    function normalizedRatingSource(source) {
        const normalized = String(source || "").toLowerCase().replace(/\s+/g, "_")
        if (normalized.indexOf("tomatoes") !== -1) return normalized.indexOf("audience") !== -1 ? "audience" : "tomatoes"
        if (normalized.indexOf("imdb") !== -1) return "imdb"
        if (normalized.indexOf("metacritic") !== -1) return "metacritic"
        if (normalized.indexOf("tmdb") !== -1) return "tmdb"
        if (normalized.indexOf("trakt") !== -1) return "trakt"
        if (normalized.indexOf("letterboxd") !== -1) return "letterboxd"
        if (normalized.indexOf("myanimelist") !== -1 || normalized === "mal") return "mal"
        if (normalized.indexOf("anilist") !== -1) return "anilist"
        if (normalized.indexOf("rogerebert") !== -1) return "rogerebert"
        if (normalized.indexOf("kinopoisk") !== -1) return "kinopoisk"
        if (normalized.indexOf("douban") !== -1) return "douban"
        return normalized
    }

    function ratingLogoSource(source, score) {
        const value = parseFloat(score) || 0
        if (source === "imdb") return "qrc:/images/ratings/imdb.png"
        if (source === "tmdb") return "qrc:/images/ratings/tmdb.png"
        if (source === "mal") return "qrc:/images/ratings/mal.png"
        if (source === "anilist") return "qrc:/images/ratings/anilist.png"
        if (source === "trakt") return "qrc:/images/ratings/trakt.png"
        if (source === "letterboxd") return "qrc:/images/ratings/letterboxd.png"
        if (source === "metacritic") return "qrc:/images/ratings/metacritic.png"
        if (source === "rogerebert") return "qrc:/images/ratings/rogerebert.png"
        if (source === "kinopoisk") return "qrc:/images/ratings/kinopoisk.png"
        if (source === "douban") return "qrc:/images/ratings/douban.png"
        if (source === "tomatoes") {
            if (value < 60) return "qrc:/images/ratings/tomatoes_rotten.png"
            if (value >= 75) return "qrc:/images/ratings/tomatoes_certified.png"
            return "qrc:/images/ratings/tomatoes.png"
        }
        if (source === "audience") {
            if (value < 60) return "qrc:/images/ratings/audience_rotten.png"
            return "qrc:/images/ratings/audience.png"
        }
        return ""
    }

    function ratingFallbackLabel(source, originalSource) {
        if (source === "imdb") return "IMDb"
        if (source === "tomatoes") return "RT"
        if (source === "audience") return "Popcorn"
        if (source === "metacritic") return "Meta"
        if (source === "mal") return "MAL"
        if (source === "anilist") return "AniList"
        return String(originalSource || "")
    }

    function ratingDisplayValue(source, score) {
        const value = parseFloat(score)
        if (!isFinite(value) || value <= 0) {
            return ""
        }
        if (source === "tomatoes" || source === "audience" || source === "rogerebert") {
            return Math.round(value) + "%"
        }
        return Number.isInteger(value) ? String(value) : value.toFixed(1)
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

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        z: -1
        clip: true

        Image {
            anchors.fill: parent
            source: backdropUrl !== "" ? backdropUrl : posterUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }

            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 0.58
                blurMax: 48
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.02, 0.02, 0.02, 0.30) }
                GradientStop { position: 0.38; color: Qt.rgba(0.03, 0.03, 0.03, 0.62) }
                GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.02, 0.02, 0.93) }
            }
        }
    }

    Item {
        anchors.fill: parent
        visible: isLoading
        z: 100

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.34)
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: isLoading
            width: Math.round(64 * Theme.layoutScale)
            height: Math.round(64 * Theme.layoutScale)
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.verticalCenter
            anchors.topMargin: Theme.spacingXLarge
            text: qsTr("Loading series details...")
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
        }
    }

    Flickable {
        id: contentFlickable
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
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
            spacing: Theme.spacingXLarge

            Rectangle {
                id: heroPanel
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(heroPosterHeight + root.heroPanelPadding * 2,
                                                 heroContent.implicitHeight + root.heroPanelPadding * 2)
                radius: Theme.radiusLarge
                color: Qt.rgba(0, 0, 0, 0.22)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.10)

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.04) }
                        GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.01) }
                    }
                }

                RowLayout {
                    id: heroContent
                    anchors.fill: parent
                    anchors.margins: root.heroPanelPadding
                    spacing: Theme.spacingXLarge

                    Item {
                        Layout.preferredWidth: heroPosterWidth
                        Layout.preferredHeight: heroPosterHeight

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.imageRadius
                            color: Theme.backgroundSecondary
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)

                            Image {
                                id: heroPosterImage
                                anchors.fill: parent
                                source: posterUrl
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true

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

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.imageRadius
                                color: Qt.rgba(0.06, 0.06, 0.06, 0.55)
                                visible: heroPosterImage.status !== Image.Ready
                            }

                            Text {
                                anchors.centerIn: parent
                                text: Icons.tvShows
                                visible: heroPosterImage.status !== Image.Ready
                                font.family: Theme.fontIcon
                                font.pixelSize: Math.round(76 * Theme.layoutScale)
                                color: Theme.textSecondary
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: Theme.spacingMedium

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: logoUrl !== "" ? Theme.detailViewLogoHeight : titleFallback.implicitHeight

                            Image {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(Theme.seriesLogoMaxWidth, parent.width)
                                height: parent.height
                                source: logoUrl
                                fillMode: Image.PreserveAspectFit
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
                                font.pixelSize: Theme.fontSizeDisplay
                                font.family: Theme.fontPrimary
                                font.weight: Font.Black
                                color: Theme.textPrimary
                                wrapMode: Text.WordWrap
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            MetadataChip {
                                visible: text !== ""
                                text: root.formatSeriesYears()
                            }

                            MetadataChip {
                                visible: text !== ""
                                text: SeriesDetailsViewModel.officialRating
                            }

                            MetadataChip {
                                visible: text !== ""
                                text: SeriesDetailsViewModel.status
                            }

                            MetadataChip {
                                visible: text !== ""
                                text: root.formatCounts()
                            }

                            Repeater {
                                model: root.visibleExternalRatings()

                                RatingMetadataChip {
                                    required property var modelData
                                    ratingData: modelData
                                    normalizedRatingSourceFn: root.normalizedRatingSource
                                    ratingLogoSourceFn: root.ratingLogoSource
                                    ratingDisplayValueFn: root.ratingDisplayValue
                                    ratingFallbackLabelFn: root.ratingFallbackLabel
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Repeater {
                                model: SeriesDetailsViewModel.genres || []

                                MetadataChip {
                                    required property var modelData
                                    text: modelData
                                }
                            }
                        }

                        Text {
                            text: root.creatorsSummary() !== ""
                                  ? qsTr("Key creatives: %1").arg(root.creatorsSummary())
                                  : ""
                            Layout.fillWidth: true
                            visible: text !== ""
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }

                        Item {
                            id: overviewContainer
                            Layout.fillWidth: true
                            Layout.minimumHeight: Math.round(148 * Theme.layoutScale)
                            Layout.preferredHeight: overviewColumn.implicitHeight

                            property bool expanded: false
                            readonly property int collapsedHeight: Math.round(150 * Theme.layoutScale)
                            property bool hasOverflow: overviewText.implicitHeight > collapsedHeight

                            ColumnLayout {
                                id: overviewColumn
                                anchors.fill: parent
                                spacing: Math.round(10 * Theme.layoutScale)

                                Item {
                                    id: overviewTextArea
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: overviewContainer.expanded
                                                            ? overviewText.implicitHeight
                                                            : Math.min(overviewText.implicitHeight, overviewContainer.collapsedHeight)
                                    clip: true

                                    Text {
                                        id: overviewText
                                        width: parent.width
                                        text: seriesOverview
                                        font.pixelSize: Theme.fontSizeBody
                                        font.family: Theme.fontPrimary
                                        font.weight: Font.Medium
                                        color: Theme.textPrimary
                                        wrapMode: Text.WordWrap
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: Math.round(56 * Theme.layoutScale)
                                        visible: !overviewContainer.expanded && overviewContainer.hasOverflow
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: "transparent" }
                                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.92) }
                                        }
                                    }
                                }

                                Button {
                                    id: readMoreButton
                                    visible: overviewContainer.hasOverflow
                                    Layout.alignment: Qt.AlignLeft
                                    padding: 0
                                    implicitHeight: Math.round(34 * Theme.layoutScale)
                                    implicitWidth: readMoreRow.implicitWidth + Math.round(22 * Theme.layoutScale)

                                    background: Rectangle {
                                        radius: Theme.radiusMedium
                                        color: {
                                            if (readMoreButton.down) return Qt.rgba(1, 1, 1, 0.18)
                                            if (readMoreButton.hovered || readMoreButton.activeFocus) return Qt.rgba(1, 1, 1, 0.13)
                                            return Qt.rgba(0, 0, 0, 0.24)
                                        }
                                        border.width: readMoreButton.activeFocus ? Theme.buttonFocusBorderWidth : 1
                                        border.color: readMoreButton.activeFocus ? Theme.buttonSecondaryBorderFocused : Qt.rgba(1, 1, 1, 0.14)
                                    }

                                    contentItem: Item {
                                        implicitWidth: readMoreRow.implicitWidth
                                        implicitHeight: readMoreRow.implicitHeight

                                        RowLayout {
                                            id: readMoreRow
                                            anchors.centerIn: parent
                                            spacing: Math.round(6 * Theme.layoutScale)

                                            Text {
                                                text: overviewContainer.expanded ? qsTr("Show Less") : qsTr("Read More")
                                                font.pixelSize: Theme.fontSizeSmall
                                                font.family: Theme.fontPrimary
                                                font.weight: Font.Black
                                                color: Theme.textPrimary
                                                Layout.alignment: Qt.AlignVCenter
                                            }

                                            Text {
                                                text: overviewContainer.expanded ? Icons.expandLess : Icons.expandMore
                                                font.family: Theme.fontIcon
                                                font.pixelSize: Theme.fontSizeIcon
                                                color: Theme.textPrimary
                                                Layout.alignment: Qt.AlignVCenter
                                            }
                                        }
                                    }

                                    onClicked: overviewContainer.expanded = !overviewContainer.expanded
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.bottomMargin: root.heroActionsBottomSpacing
                            spacing: Theme.spacingMedium

                            Button {
                                id: playButton
                                text: hasNextEpisode ? qsTr("Play Next Episode") : qsTr("Start Series")
                                enabled: seriesId !== ""
                                Layout.preferredHeight: Theme.buttonHeightLarge
                                Layout.preferredWidth: Math.round(300 * Theme.layoutScale)

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
                                                             SeriesDetailsViewModel.nextEpisodePlaybackPositionTicks)
                                        return
                                    }

                                    let firstRealSeasonIndex = -1
                                    const count = SeriesDetailsViewModel.seasonsModel.rowCount()
                                    for (let i = 0; i < count; ++i) {
                                        const season = SeriesDetailsViewModel.seasonsModel.getItem(i)
                                        if (season && (season.IndexNumber >= 1 || season.indexNumber >= 1)) {
                                            firstRealSeasonIndex = i
                                            break
                                        }
                                    }
                                    root.navigateToSeasons(firstRealSeasonIndex >= 0 ? firstRealSeasonIndex : 0)
                                }

                                background: Rectangle {
                                    radius: Theme.radiusMedium
                                    gradient: Gradient {
                                        GradientStop {
                                            position: 0.0
                                            color: !playButton.enabled
                                                   ? Qt.rgba(0.12, 0.12, 0.12, 0.55)
                                                   : (playButton.down
                                                      ? Theme.buttonPrimaryBackgroundPressed
                                                      : playButton.hovered
                                                        ? Theme.buttonPrimaryBackgroundHover
                                                        : Theme.buttonPrimaryBackground)
                                        }
                                        GradientStop {
                                            position: 1.0
                                            color: !playButton.enabled
                                                   ? Qt.rgba(0.08, 0.08, 0.08, 0.55)
                                                   : (playButton.down
                                                      ? Qt.darker(Theme.buttonPrimaryBackgroundPressed, 1.1)
                                                      : playButton.hovered
                                                        ? Qt.darker(Theme.buttonPrimaryBackgroundHover, 1.08)
                                                        : Qt.darker(Theme.buttonPrimaryBackground, 1.12))
                                        }
                                    }
                                    border.width: playButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                                    border.color: playButton.activeFocus ? Theme.buttonPrimaryBorderFocused : Qt.rgba(1, 1, 1, 0.12)

                                    Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                                }

                                contentItem: RowLayout {
                                    implicitWidth: playInnerRow.implicitWidth
                                    implicitHeight: playInnerRow.implicitHeight
                                    anchors.centerIn: parent

                                    RowLayout {
                                        id: playInnerRow
                                        anchors.centerIn: parent
                                        spacing: Theme.spacingSmall

                                        Text {
                                            text: Icons.playArrow
                                            font.family: Theme.fontIcon
                                            font.pixelSize: Theme.fontSizeIcon
                                            color: Theme.textPrimary
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            Layout.alignment: Qt.AlignVCenter
                                        }

                                        Text {
                                            text: playButton.text
                                            font.pixelSize: Theme.fontSizeBody
                                            font.family: Theme.fontPrimary
                                            font.weight: Font.Black
                                            color: Theme.textPrimary
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            Layout.alignment: Qt.AlignVCenter
                                        }
                                    }
                                }
                            }

                            SecondaryActionButton {
                                id: favoriteButton
                                text: isFavorite ? qsTr("Favorited") : qsTr("Favorite")
                                iconGlyph: isFavorite ? Icons.favorite : Icons.favoriteBorder
                                iconColor: isFavorite ? "#e74c3c" : Theme.textPrimary
                                Layout.preferredHeight: Theme.buttonHeightLarge

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
                                iconGlyph: Icons.moreVert
                                implicitWidth: Theme.buttonIconSize
                                Layout.preferredHeight: Theme.buttonHeightLarge
                                Layout.preferredWidth: Theme.buttonIconSize

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

                                Keys.onReturnPressed: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)
                                Keys.onEnterPressed: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)
                                onClicked: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)

                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("More options")
                                ToolTip.delay: 500
                            }

                            Item { Layout.fillWidth: true }
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
                    font.weight: Font.Black
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
                            color: Qt.rgba(1, 1, 1, 0.06)
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
                                    color: Qt.rgba(0.08, 0.08, 0.08, 0.45)
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

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width + border.width * 2
                                        height: parent.height + border.width * 2
                                        radius: Theme.imageRadius + border.width
                                        color: "transparent"
                                        border.width: seasonDelegate.isFocused ? Theme.buttonFocusBorderWidth : 0
                                        border.color: Theme.accentPrimary
                                        visible: border.width > 0
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
                        font.weight: Font.Black
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
                        font.weight: Font.Black
                        color: Theme.textPrimary
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(120 * Theme.layoutScale)
                        visible: libraryRecommendationsLoading && libraryRecommendations.length === 0
                        radius: Theme.radiusMedium
                        color: Qt.rgba(1, 1, 1, 0.05)
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
                        font.weight: Font.Black
                        color: Theme.textPrimary
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(120 * Theme.layoutScale)
                        visible: seerrRecommendationsLoading && seerrRecommendedItems.length === 0
                        radius: Theme.radiusMedium
                        color: Qt.rgba(1, 1, 1, 0.05)
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

    Connections {
        target: ResponsiveLayoutManager

        function onBreakpointChanged() {
            root.savedSeasonIndex = seasonsGrid.currentIndex
            root.savedFocusItem = root.Window.activeFocusItem
            Qt.callLater(root.restoreFocusAfterBreakpoint)
        }
    }

    function restoreFocusAfterBreakpoint() {
        if (savedSeasonIndex >= 0 && seasonsGrid.count > 0) {
            seasonsGrid.currentIndex = Math.min(savedSeasonIndex, seasonsGrid.count - 1)
            seasonsGrid.positionViewAtIndex(seasonsGrid.currentIndex, GridView.Contain)
        }

        if (savedFocusItem && savedFocusItem.parent && typeof savedFocusItem.forceActiveFocus === "function") {
            savedFocusItem.forceActiveFocus()
        } else if (playButton.enabled) {
            playButton.forceActiveFocus()
        } else {
            root.focusFirstLowerSection()
        }

        savedFocusItem = null
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

    Menu {
        id: contextMenu

        background: Rectangle {
            implicitWidth: Math.round(280 * Theme.layoutScale)
            color: Theme.cardBackground
            radius: Theme.radiusMedium
            border.color: Theme.cardBorder
            border.width: 1

            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 4
                shadowBlur: 0.5
                shadowColor: "#44000000"
            }
        }

        delegate: MenuItem {
            id: menuItem
            implicitWidth: Math.round(240 * Theme.layoutScale)
            implicitHeight: Math.round(40 * Theme.layoutScale)

            arrow: Canvas {
                x: parent.width - width - 12
                y: parent.height / 2 - height / 2
                width: 12
                height: 12
                visible: menuItem.subMenu
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.fillStyle = menuItem.highlighted ? Theme.textPrimary : Theme.textSecondary
                    ctx.moveTo(0, 0)
                    ctx.lineTo(0, height)
                    ctx.lineTo(width, height / 2)
                    ctx.closePath()
                    ctx.fill()
                }
            }

            contentItem: Text {
                text: menuItem.text
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: menuItem.highlighted ? Theme.textPrimary : Theme.textSecondary
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.spacingSmall
                rightPadding: menuItem.arrow.width + 12
            }

            background: Rectangle {
                implicitWidth: Math.round(240 * Theme.layoutScale)
                implicitHeight: Math.round(40 * Theme.layoutScale)
                opacity: enabled ? 1 : 0.3
                color: menuItem.highlighted ? Theme.hoverOverlay : "transparent"
                radius: Theme.radiusSmall
            }
        }

        onOpened: {
            currentIndex = 0
            forceActiveFocus()
        }

        onClosed: {
            Qt.callLater(function() {
                contextMenuButton.forceActiveFocus()
            })
        }

        MenuItem {
            id: watchedMenuItem
            text: isWatched ? qsTr("Mark as Unwatched") : qsTr("Mark as Watched")

            contentItem: Text {
                text: watchedMenuItem.text
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: watchedMenuItem.highlighted ? Theme.textPrimary : Theme.textSecondary
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.spacingSmall
                rightPadding: Theme.spacingSmall
            }

            background: Rectangle {
                implicitWidth: Math.round(240 * Theme.layoutScale)
                implicitHeight: Math.round(40 * Theme.layoutScale)
                opacity: watchedMenuItem.enabled ? 1 : 0.3
                color: watchedMenuItem.highlighted ? Theme.hoverOverlay : "transparent"
                radius: Theme.radiusSmall
            }

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

        Menu {
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

            background: Rectangle {
                implicitWidth: Math.round(280 * Theme.layoutScale)
                color: Theme.cardBackground
                radius: Theme.radiusMedium
                border.color: Theme.cardBorder
                border.width: 1
            }

            MenuItem {
                text: profileSubmenu.currentProfile === "" ? qsTr("Current: Use Default")
                                                           : qsTr("Current: %1").arg(profileSubmenu.currentProfile)
                enabled: false

                contentItem: Text {
                    text: parent.text
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    font.italic: true
                }

                background: Rectangle { color: "transparent" }
            }

            MenuSeparator {
                contentItem: Rectangle {
                    implicitHeight: 1
                    color: Theme.borderLight
                }
            }

            MenuItem {
                text: qsTr("Use Default")

                contentItem: RowLayout {
                    spacing: Theme.spacingSmall

                    Text {
                        text: profileSubmenu.currentProfile === "" ? "✓" : "  "
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.accentPrimary
                        Layout.preferredWidth: Math.round(20 * Theme.layoutScale)
                    }

                    Text {
                        text: qsTr("Use Default")
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }
                }

                background: Rectangle {
                    color: parent.highlighted ? Theme.hoverOverlay : "transparent"
                    radius: Theme.radiusSmall
                }

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

                MenuItem {
                    required property string modelData

                    text: modelData

                    contentItem: RowLayout {
                        spacing: Theme.spacingSmall

                        Text {
                            text: profileSubmenu.currentProfile === modelData ? "✓" : "  "
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.accentPrimary
                            Layout.preferredWidth: Math.round(20 * Theme.layoutScale)
                        }

                        Text {
                            text: modelData
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }
                    }

                    background: Rectangle {
                        color: parent.highlighted ? Theme.hoverOverlay : "transparent"
                        radius: Theme.radiusSmall
                    }

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
