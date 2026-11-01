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
    property var requestRecommendationCacheStore: null
    property int requestRecommendationCacheTtlMs: 10 * 60 * 1000
    readonly property bool isRestoringReturnFocus: restoringFocusFromNavigation
                                                 || restoringFocusFromReturnState
                                                 || suppressHeroAutofocus
                                                 || hasPendingReturnStateForCurrentSeries()

    property var requestRecommendedItems: []
    property bool requestRecommendationsLoading: false
    property string requestPendingTmdbId: ""
    property string requestLoadedTmdbId: ""

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
    // Local extras; Play Trailer shows only when one of them is a trailer.
    readonly property var extras: SeriesDetailsViewModel.extras || []
    readonly property var trailer: SeriesDetailsViewModel.trailer || ({})
    readonly property bool hasTrailer: !!(trailer && trailer.itemId)
    readonly property int extraCardImageWidth: Math.round(Theme.landscapeCardWidth * Math.max(2, Screen.devicePixelRatio))
    // The card or button that started an extra gets focus back afterwards.
    property Item extraReturnFocusTarget: null
    property bool extraReturnFocusPending: false
    property bool extraReturnFocusActivated: false
    readonly property var libraryRecommendations: SeriesDetailsViewModel.similarItems || []
    readonly property bool libraryRecommendationsLoading: SeriesDetailsViewModel.similarItemsLoading
    readonly property bool requestsAvailable: typeof MediaRequests !== "undefined" && MediaRequests !== null && MediaRequests.available
    readonly property string similarMediaType: "tv" // SeriesDetailsView only issues TV similar-title requests.
    readonly property var externalRatings: SeriesDetailsViewModel.mdbListRatings || ({})
    // Ratings come from MDBList (with an API key) and, for anime, AniList.
    // Mirrors the fetch conditions in SeriesDetailsViewModel.
    readonly property bool expectsExternalRatings: {
        if (ConfigManager.mdbListApiKey !== "")
            return true
        const genres = SeriesDetailsViewModel.genres || []
        for (let i = 0; i < genres.length; ++i) {
            const genre = String(genres[i]).toLowerCase()
            if (genre === "anime" || genre === "animation")
                return true
        }
        return false
    }

    // Hero copy keeps a fixed reading measure (about half a 16:9 screen), so
    // ultrawide screens do not stretch it.
    readonly property int heroBlockWidth: Math.min(Theme.heroCopyMaxWidth,
                                                   root.width - Theme.pageMarginX * 2)
    readonly property int peopleCardWidth: Math.round(176 * Theme.layoutScale)
    readonly property int peopleCardHeight: Math.round(320 * Theme.layoutScale)
    // Request sharper artwork for the scaled cards at high display scaling.
    readonly property int cardImageWidth: Math.round(Theme.posterCardWidth * Math.max(2, Screen.devicePixelRatio))
    readonly property int shelfScrollOffset: Math.round(56 * Theme.layoutScale)
    /// The navigation bar recedes once the page scrolls into the shelves.
    readonly property bool navigationRecessed: contentFlickable.contentY > Theme.navBarReservedHeight

    signal navigateToSeasons(int seasonIndex)
    signal playNextEpisode(string episodeId, var startPositionMs)
    signal extraPlayRequested(var request)
    signal navigateToEpisode(var episodeData)
    signal itemSelected(var itemData)
    signal backRequested()
    signal returnStateConsumed()

    focus: true

    // Shelves below the hero, in focus order (visible ones only).
    function shelfTargets() {
        const targets = []
        if (seasonsSection.visible && seasonsList.count > 0) targets.push(seasonsList)
        if (extrasSection.visible && extrasList.count > 0) targets.push(extrasList)
        if (castSection.visible && castList.count > 0) targets.push(castList)
        if (libraryRecommendationsSection.visible && libraryRecommendationsList.count > 0)
            targets.push(libraryRecommendationsList)
        if (requestRecommendationsSection.visible && requestRecommendationsList.count > 0)
            targets.push(requestRecommendationsList)
        return targets
    }

    function focusFirstLowerSection() {
        const targets = shelfTargets()
        if (targets.length > 0) {
            targets[0].forceActiveFocus()
        }
    }

    // Moves focus to the shelf above or below. Above the first shelf is the
    // hero's primary action.
    function moveShelfFocus(fromList, delta) {
        const targets = shelfTargets()
        const index = targets.indexOf(fromList)
        const target = index + delta
        if (index >= 0 && target >= 0 && target < targets.length) {
            targets[target].forceActiveFocus()
        } else if (target < 0 && playButton.enabled) {
            playButton.forceActiveFocus()
        }
        return true
    }

    // Up from the action row: Read More when shown, otherwise leave the key
    // unhandled so it reaches the navigation bar.
    function heroUp(event) {
        if (detailHero.readMoreButton.visible) {
            detailHero.readMoreButton.forceActiveFocus()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    function scrollTo(y) {
        const maxY = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
        scrollAnimation.to = Math.max(0, Math.min(maxY, y))
        scrollAnimation.restart()
    }

    function ensureTopVisible() {
        scrollTo(0)
    }

    // The first shelf scrolls only as far as it needs to sit fully in view
    // under the hero; later shelves land in the same spot near the top.
    function scrollToSection(section) {
        if (!section) {
            return
        }
        const targets = shelfTargets()
        const first = targets.length > 0 ? targets[0].parent : null
        const top = section.mapToItem(contentColumn, 0, 0).y
        if (section === first) {
            scrollTo(top + section.height + Theme.spacingLarge - contentFlickable.height)
        } else {
            scrollTo(top - root.shelfScrollOffset)
        }
    }

    function extraKindLabel(kind) {
        switch (kind) {
        case "trailer": return qsTr("Trailer")
        case "teaser": return qsTr("Teaser")
        case "featurette": return qsTr("Featurette")
        case "clip": return qsTr("Clip")
        case "behindTheScenes": return qsTr("Behind the Scenes")
        case "bloopers": return qsTr("Bloopers")
        case "deletedScene": return qsTr("Deleted Scene")
        case "interview": return qsTr("Interview")
        case "scene": return qsTr("Scene")
        case "short": return qsTr("Short")
        default: return qsTr("Extra")
        }
    }

    function extraTitle(extra) {
        return extra && extra.name ? extra.name : extraKindLabel(extra ? extra.extraKind : "")
    }

    function formatExtraDuration(ms) {
        const total = Math.max(0, Math.floor((ms || 0) / 1000))
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const seconds = total % 60
        function pad(value) { return value < 10 ? "0" + value : "" + value }
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds)
    }

    // "Trailer · 2:31"; the kind is left out when it is already the title.
    function extraSubtitle(extra) {
        if (!extra) {
            return ""
        }
        const parts = []
        if (extra.name) {
            parts.push(extraKindLabel(extra.extraKind))
        }
        if (extra.durationMs > 0) {
            parts.push(formatExtraDuration(extra.durationMs))
        }
        return parts.join(" · ")
    }

    // Extras play on their own: from the start, without the series' Up Next,
    // episode navigation or track choices (PlayerPolicy content rules). In a
    // merged space the owning connection goes live first.
    function playExtra(extra, restoreFocusTarget) {
        if (!extra || !extra.itemId) {
            return
        }
        extrasRouter.run(extra.connectionId, function() {
            extraReturnFocusTarget = restoreFocusTarget || trailerButton
            extraReturnFocusPending = true
            extraReturnFocusActivated = false
            root.extraPlayRequested({
                itemId: extra.itemId,
                contentKind: "extra",
                startPositionMs: 0,
                seriesId: "",
                seasonId: "",
                overlayTitle: seriesName || qsTr("Now Playing"),
                overlaySubtitle: extraTitle(extra),
                overlayBackdropUrl: backdropUrl,
                overlayLogoUrl: logoUrl,
                preferredAudioIndex: -2,
                preferredSubtitleIndex: -2,
                isMovie: true,
                allowVersionPrompt: false,
                restoreFocusTarget: extraReturnFocusTarget
            })
        })
    }

    function resetExtraReturnFocus() {
        extraReturnFocusPending = false
        extraReturnFocusActivated = false
        extraReturnFocusTarget = null
    }

    function restoreFocusAfterExtra() {
        const target = extraReturnFocusTarget
        resetExtraReturnFocus()
        if (!root.visible) {
            return
        }
        if (target && target.parent && target.visible && typeof target.forceActiveFocus === "function") {
            target.forceActiveFocus()
        } else if (playButton.enabled) {
            playButton.forceActiveFocus()
        } else {
            focusFirstLowerSection()
        }
    }

    function yearOf(item) {
        if (!item) {
            return ""
        }
        if (item.productionYear) {
            return String(item.productionYear)
        }
        if (item.premiereDate) {
            const date = new Date(item.premiereDate)
            if (!isNaN(date.getTime())) {
                return String(date.getFullYear())
            }
        }
        return ""
    }

    function formatSeriesYears() {
        const startYear = SeriesDetailsViewModel.productionYear
        const endDate = SeriesDetailsViewModel.endDate
        const endYear = endDate && !isNaN(endDate.getTime()) ? endDate.getFullYear() : 0

        if (startYear <= 0) {
            return ""
        }
        if (SeriesDetailsViewModel.status === "Ended") {
            return endYear > 0 && endYear !== startYear ? startYear + " – " + endYear : String(startYear)
        }
        return qsTr("%1 – Present").arg(startYear)
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
                || itemIsDescendant(activeItem, trailerButton)
                || itemIsDescendant(activeItem, favoriteButton)
                || itemIsDescendant(activeItem, watchlistButton)
                || itemIsDescendant(activeItem, contextMenuButton)) {
            return "hero"
        }
        if (itemIsDescendant(activeItem, seasonsList)) {
            return "seasons"
        }
        if (itemIsDescendant(activeItem, extrasList)) {
            return "extras"
        }
        if (itemIsDescendant(activeItem, castList)) {
            return "cast"
        }
        if (itemIsDescendant(activeItem, libraryRecommendationsList)) {
            return "libraryRecommendations"
        }
        if (itemIsDescendant(activeItem, requestRecommendationsList)) {
            return "requestRecommendations"
        }
        return "hero"
    }

    function currentFocusIndex() {
        const area = currentFocusArea()
        const activeItem = root.Window.activeFocusItem

        if (area === "hero") {
            if (itemIsDescendant(activeItem, favoriteButton)) return 1
            if (itemIsDescendant(activeItem, contextMenuButton)) return 2
            if (itemIsDescendant(activeItem, watchlistButton)) return 3
            if (itemIsDescendant(activeItem, trailerButton)) return 4
            return 0
        }
        if (area === "seasons") return Math.max(0, seasonsList.currentIndex)
        if (area === "extras") return Math.max(0, extrasList.currentIndex)
        if (area === "cast") return Math.max(0, castList.currentIndex)
        if (area === "libraryRecommendations") return Math.max(0, libraryRecommendationsList.currentIndex)
        if (area === "requestRecommendations") return Math.max(0, requestRecommendationsList.currentIndex)
        return 0
    }

    function saveReturnState() {
        return {
            seriesId: seriesId,
            focusArea: currentFocusArea(),
            focusIndex: currentFocusIndex(),
            seasonsGridIndex: Math.max(0, seasonsList.currentIndex),
            contentY: contentFlickable.contentY,
            requestTmdbId: String(tmdbId || requestLoadedTmdbId || ""),
            requestRecommendations: (requestRecommendedItems || []).slice()
        }
    }

    function restoreFocusToArea(area, index) {
        const targetIndex = Math.max(0, index || 0)

        if (area === "hero") {
            if (targetIndex === 1 && favoriteButton.visible) {
                favoriteButton.forceActiveFocus()
            } else if (targetIndex === 2 && contextMenuButton.visible) {
                contextMenuButton.forceActiveFocus()
            } else if (targetIndex === 3 && watchlistButton.visible) {
                watchlistButton.forceActiveFocus()
            } else if (targetIndex === 4 && trailerButton.visible) {
                trailerButton.forceActiveFocus()
            } else {
                playButton.forceActiveFocus()
            }
            return true
        }

        const lists = {
            "seasons": seasonsList,
            "extras": extrasList,
            "cast": castList,
            "libraryRecommendations": libraryRecommendationsList,
            "requestRecommendations": requestRecommendationsList
        }
        const list = lists[area]
        if (list && list.parent.visible && list.count > 0) {
            list.currentIndex = Math.min(targetIndex, list.count - 1)
            list.forceActiveFocus()
            return true
        }

        return false
    }

    function restoreReturnState(state) {
        if (!state || String(state.seriesId || "") !== String(seriesId || "")) {
            return false
        }

        if (state.requestRecommendations && state.requestRecommendations.length > 0) {
            requestRecommendedItems = state.requestRecommendations.slice()
            requestLoadedTmdbId = String(state.requestTmdbId || "")
            requestPendingTmdbId = ""
            requestRecommendationsLoading = false
        }

        if (state.contentY !== undefined && state.contentY >= 0) {
            const maxScroll = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
            contentFlickable.contentY = Math.min(state.contentY, maxScroll)
        }

        const desiredArea = state.focusArea || "hero"
        if (desiredArea === "seasons" && seasonsList.count <= 0) return false
        if (desiredArea === "extras" && extrasList.count <= 0) return false
        if (desiredArea === "cast" && castList.count <= 0) return false
        if (desiredArea === "libraryRecommendations" && libraryRecommendations.length <= 0) return false
        if (desiredArea === "requestRecommendations" && requestRecommendedItems.length <= 0) return false

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

    // Seerr and Silo answer differently, so each backend has its own entries.
    function recommendationCacheKey(requestedTmdbId) {
        return MediaRequests.backend + ":" + requestedTmdbId
    }

    function loadRequestRecommendations() {
        const requestedTmdbId = String(tmdbId || "")

        if (!requestsAvailable || requestedTmdbId === "" || Number(requestedTmdbId) <= 0) {
            requestPendingTmdbId = ""
            return
        }

        if (requestRecommendationCacheStore) {
            const cachedEntry = requestRecommendationCacheStore[recommendationCacheKey(requestedTmdbId)]
            const now = Date.now()
            if (cachedEntry && cachedEntry.timestamp && (now - cachedEntry.timestamp) <= requestRecommendationCacheTtlMs) {
                requestRecommendedItems = (cachedEntry.items || []).slice()
                requestLoadedTmdbId = requestedTmdbId
                requestPendingTmdbId = ""
                requestRecommendationsLoading = false
                return
            }
        }

        if ((requestRecommendationsLoading && requestPendingTmdbId === requestedTmdbId)
                || requestLoadedTmdbId === requestedTmdbId) {
            return
        }

        requestPendingTmdbId = ""
        requestLoadedTmdbId = ""
        requestRecommendedItems = []
        requestRecommendationsLoading = false
        requestPendingTmdbId = requestedTmdbId
        requestRecommendationsLoading = true
        MediaRequests.getSimilar(similarMediaType, Number(requestedTmdbId))
    }

    function restorePendingSeasonFocus() {
        if (root.pendingSeasonsGridIndex < 0 || seasonsList.count <= 0) {
            if (seasonsList.count > 0 && seasonsList.currentIndex < 0) {
                seasonsList.currentIndex = 0
            }
            return
        }

        const targetIndex = Math.min(root.pendingSeasonsGridIndex, seasonsList.count - 1)
        seasonsList.currentIndex = targetIndex
        seasonsList.forceActiveFocus()
        root.pendingSeasonsGridIndex = -1
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
        if ((restoringFocusFromNavigation && savedFocusItem)
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
                && pendingReturnState.requestRecommendations
                && pendingReturnState.requestRecommendations.length > 0) {
            requestRecommendedItems = pendingReturnState.requestRecommendations.slice()
            requestRecommendationsLoading = false
            requestPendingTmdbId = ""
            requestLoadedTmdbId = String(pendingReturnState.requestTmdbId || "")
        } else {
            requestRecommendedItems = []
            requestRecommendationsLoading = false
            requestPendingTmdbId = ""
            requestLoadedTmdbId = ""
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

    onRequestRecommendedItemsChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onPendingSeasonsGridIndexChanged: {
        if (pendingSeasonsGridIndex >= 0) {
            Qt.callLater(root.restorePendingSeasonFocus)
        }
    }

    // Stage: the series backdrop, sharp behind the hero copy. A poster
    // fallback is low resolution, so it is shown softened instead.
    StageBackdrop {
        id: stage
        anchors.fill: parent
        z: -1
        mode: backdropUrl !== "" ? "hero" : "ambient"
        source: backdropUrl !== "" ? backdropUrl : posterUrl
        // Quiet the artwork as the page scrolls into the shelves.
        dim: Math.min(0.72, contentFlickable.contentY / Math.max(1, heroArea.height) * 0.72)
    }

    StateMessage {
        anchors.centerIn: parent
        visible: isLoading && seriesName === ""
        z: 100
        kind: "loading"
        message: qsTr("Loading series details…")
    }

    NumberAnimation {
        id: scrollAnimation
        target: contentFlickable
        property: "contentY"
        duration: Theme.durationLong
        easing.type: Theme.easingStandard
    }

    // The hero and the shelves scroll together, like Home.
    Flickable {
        id: contentFlickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: contentColumn
            width: contentFlickable.width
            spacing: Theme.sectionSpacing

            // Hero: about the top 60% of the screen. The copy sits at its foot
            // and grows upwards, never under the navigation bar.
            Item {
                id: heroArea
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Math.round(root.height * 0.60),
                                                 Theme.navBarReservedHeight + detailHero.height)

                DetailHero {
                    id: detailHero
                    x: Theme.pageMarginX
                    anchors.bottom: parent.bottom
                    width: root.heroBlockWidth
                    eyebrow: qsTr("Series")
                    logoSource: logoUrl
                    title: seriesName
                    metadataItems: [root.formatSeriesYears(), SeriesDetailsViewModel.officialRating, root.formatCounts()]
                    ratings: root.visibleExternalRatings()
                    // Ratings and the up-next episode load after the details.
                    reserveChipRow: root.expectsExternalRatings
                    reserveFootnote: true
                    detailItems: SeriesDetailsViewModel.genres || []
                    detailText: root.creatorsSummary() !== ""
                                ? qsTr("Key creatives: %1").arg(root.creatorsSummary())
                                : ""
                    synopsis: seriesOverview
                    primaryAction: playButton
                    // The episode the primary action will play.
                    footnote: hasNextEpisode && SeriesDetailsViewModel.nextEpisodeName !== ""
                              ? qsTr("Up next: %1").arg(SeriesDetailsViewModel.nextEpisodeName)
                              : ""

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
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(40 * Theme.layoutScale)

                        KeyNavigation.right: trailerButton.visible ? trailerButton : favoriteButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

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
                            root.resetExtraReturnFocus()
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

                    // Plays the series' local trailer; remote trailers are not offered.
                    SecondaryActionButton {
                        id: trailerButton
                        visible: root.hasTrailer
                        text: qsTr("Play Trailer")
                        iconGlyph: Icons.movie
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(36 * Theme.layoutScale)

                        KeyNavigation.left: playButton
                        KeyNavigation.right: favoriteButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

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

                        onClicked: root.playExtra(root.trailer, trailerButton)
                    }

                    SecondaryActionButton {
                        id: favoriteButton
                        text: isFavorite ? qsTr("Favorited") : qsTr("Favorite")
                        iconGlyph: isFavorite ? Icons.favorite : Icons.favoriteBorder
                        iconColor: isFavorite ? Theme.favoriteColor : Theme.textPrimary
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(36 * Theme.layoutScale)

                        KeyNavigation.left: trailerButton.visible ? trailerButton : playButton
                        KeyNavigation.right: watchlistButton.visible ? watchlistButton : contextMenuButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

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

                    // Only providers with a watchlist (Silo) show this.
                    SecondaryActionButton {
                        id: watchlistButton
                        visible: SeriesDetailsViewModel.watchlistSupported
                        text: SeriesDetailsViewModel.inWatchlist ? qsTr("On Watchlist") : qsTr("Add to Watchlist")
                        iconGlyph: SeriesDetailsViewModel.inWatchlist ? Icons.bookmark : Icons.bookmarkBorder
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(36 * Theme.layoutScale)

                        KeyNavigation.left: favoriteButton
                        KeyNavigation.right: contextMenuButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

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

                        onClicked: SeriesDetailsViewModel.toggleWatchlist()
                    }

                    SecondaryActionButton {
                        id: contextMenuButton
                        text: ""
                        accessibleLabel: qsTr("More options")
                        iconGlyph: Icons.moreHoriz
                        implicitHeight: Theme.buttonHeightHero
                        iconPixelSize: Theme.iconSizeLarge

                        KeyNavigation.left: watchlistButton.visible ? watchlistButton : favoriteButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

                        Keys.onDownPressed: (event) => {
                            root.focusFirstLowerSection()
                            event.accepted = true
                        }

                        Keys.onRightPressed: (event) => { event.accepted = true }
                        Keys.onReturnPressed: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height + Theme.spacingSmall)
                        Keys.onEnterPressed: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height + Theme.spacingSmall)
                        onClicked: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height + Theme.spacingSmall)
                    }
                }
            }

            ColumnLayout {
                id: seasonsSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: seasonsList.count > 0

                SectionHeader {
                    text: qsTr("Seasons")
                    detail: SeriesDetailsViewModel.seasonCount > 1
                            ? qsTr("%1 seasons").arg(SeriesDetailsViewModel.seasonCount) : ""
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: seasonsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: SeriesDetailsViewModel.seasonsModel

                    delegate: MediaCard {
                        required property int index
                        required property string name
                        required property string imageUrl
                        required property int episodeCount
                        required property int unplayedItemCount
                        required property bool isPlayed
                        variant: "poster"
                        // Seasons without art fall back to the series poster.
                        artworkSources: [imageUrl, posterUrl]
                        title: name
                        subtitle: episodeCount === 1 ? qsTr("1 Episode")
                                  : (episodeCount > 0 ? qsTr("%1 Episodes").arg(episodeCount) : "")
                        placeholderIcon: Icons.tvShows
                        showWatchState: true
                        unplayedCount: unplayedItemCount
                        watched: isPlayed
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onClicked: {
                            seasonsList.currentIndex = index
                            seasonsList.forceActiveFocus()
                            root.navigateToSeasons(index)
                        }
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(seasonsSection)
                    onCurrentIndexChanged: {
                        if (activeFocus && currentIndex >= 0) {
                            SeriesDetailsViewModel.prefetchSeasonsAround(currentIndex, 2)
                        }
                    }
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(seasonsList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(seasonsList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < count)
                            root.navigateToSeasons(currentIndex)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < count)
                            root.navigateToSeasons(currentIndex)
                        event.accepted = true
                    }
                }
            }

            ColumnLayout {
                id: extrasSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: seriesId !== "" && extras.length > 0

                SectionHeader {
                    text: qsTr("Extras")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: extrasList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    cardHeight: Theme.landscapeCardHeight
                    model: extras

                    delegate: MediaCard {
                        required property var modelData
                        required property int index
                        variant: "landscape"
                        artworkSources: [modelData.primaryArtwork && modelData.primaryArtwork.itemId
                                         ? LibraryService.getCachedArtworkUrlFromRef(modelData.primaryArtwork, root.extraCardImageWidth)
                                         : ""]
                        title: root.extraTitle(modelData)
                        subtitle: root.extraSubtitle(modelData)
                        placeholderIcon: Icons.movie
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onClicked: {
                            extrasList.currentIndex = index
                            extrasList.forceActiveFocus()
                            root.playExtra(modelData, extrasList)
                        }
                        Accessible.description: qsTr("Play %1").arg(root.extraTitle(modelData))
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(extrasSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(extrasList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(extrasList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < extras.length)
                            root.playExtra(extras[currentIndex], extrasList)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < extras.length)
                            root.playExtra(extras[currentIndex], extrasList)
                        event.accepted = true
                    }
                }
            }

            ColumnLayout {
                id: castSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: castAndCrew.length > 0

                SectionHeader {
                    text: qsTr("Cast & Crew")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: castList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    cardHeight: root.peopleCardHeight
                    model: castAndCrew

                    delegate: PersonCard {
                        required property var modelData
                        required property int index
                        width: root.peopleCardWidth
                        height: root.peopleCardHeight
                        itemData: modelData
                        isFocused: ListView.isCurrentItem && ListView.view.activeFocus
                        onActiveFocusChanged: if (activeFocus) castList.currentIndex = index
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(castSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(castList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(castList, 1) }
                    Keys.onReturnPressed: (event) => { event.accepted = true }
                    Keys.onEnterPressed: (event) => { event.accepted = true }
                }
            }

            ColumnLayout {
                id: libraryRecommendationsSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: libraryRecommendations.length > 0

                SectionHeader {
                    text: qsTr("Recommended From Your Library")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: libraryRecommendationsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: libraryRecommendations

                    delegate: MediaCard {
                        required property var modelData
                        required property int index
                        variant: "poster"
                        artworkSources: [modelData.primaryArtwork && modelData.primaryArtwork.itemId
                                         ? LibraryService.getCachedArtworkUrlFromRef(modelData.primaryArtwork, root.cardImageWidth)
                                         : ""]
                        title: modelData.name || ""
                        subtitle: root.yearOf(modelData)
                        placeholderIcon: modelData.mediaType === "Series" ? Icons.tvShows : Icons.movie
                        item: modelData
                        showWatchState: modelData.mediaType === "Series" || !!modelData.watched
                        unplayedCount: modelData.mediaType === "Series" ? (modelData.unplayedItemCount || 0) : 0
                        watched: !!modelData.watched
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onClicked: {
                            libraryRecommendationsList.currentIndex = index
                            libraryRecommendationsList.forceActiveFocus()
                            root.itemSelected(modelData)
                        }
                        Accessible.description: qsTr("Details for %1").arg(modelData.name || "")
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(libraryRecommendationsSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(libraryRecommendationsList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(libraryRecommendationsList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < libraryRecommendations.length)
                            root.itemSelected(libraryRecommendations[currentIndex])
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < libraryRecommendations.length)
                            root.itemSelected(libraryRecommendations[currentIndex])
                        event.accepted = true
                    }
                }
            }

            ColumnLayout {
                id: requestRecommendationsSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: requestRecommendedItems.length > 0

                SectionHeader {
                    text: qsTr("More to Request")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: requestRecommendationsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: requestRecommendedItems

                    delegate: MediaCard {
                        required property var modelData
                        required property int index
                        variant: "poster"
                        artworkSources: [modelData.imageUrl || ""]
                        title: modelData.name || ""
                        subtitle: root.yearOf(modelData)
                        placeholderIcon: String(modelData.requestMediaType || "").toLowerCase() === "movie"
                                         ? Icons.movie : Icons.tvShows
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onClicked: {
                            requestRecommendationsList.currentIndex = index
                            requestRecommendationsList.forceActiveFocus()
                            requestDialog.openForItem(modelData, requestRecommendationsList)
                        }
                        Accessible.description: qsTr("Request %1").arg(modelData.name || "")
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(requestRecommendationsSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(requestRecommendationsList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(requestRecommendationsList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < requestRecommendedItems.length)
                            requestDialog.openForItem(requestRecommendedItems[currentIndex], requestRecommendationsList)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < requestRecommendedItems.length)
                            requestDialog.openForItem(requestRecommendedItems[currentIndex], requestRecommendationsList)
                        event.accepted = true
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.pageMarginBottom }
        }
    }

    WheelStepScroller {
        anchors.fill: contentFlickable
        target: contentFlickable
        stepPx: Math.round(140 * Theme.layoutScale)
    }

    MediaRequestDialog {
        id: requestDialog
        parent: Overlay.overlay
    }

    ToastNotification {
        id: watchlistToast
        z: 300
    }

    SourceRouter {
        id: extrasRouter
        onFailed: (message) => watchlistToast.show(message || qsTr("Couldn't reach the server for this extra."))
    }

    Connections {
        target: PlayerController

        function onIsPlaybackActiveChanged() {
            if (!root.extraReturnFocusPending) {
                return
            }
            if (PlayerController.isPlaybackActive) {
                root.extraReturnFocusActivated = true
                return
            }
            if (root.extraReturnFocusActivated) {
                Qt.callLater(root.restoreFocusAfterExtra)
            }
        }
    }

    Connections {
        target: SeriesDetailsViewModel

        function onSeriesLoaded() {
            if (!hasPendingReturnStateForCurrentSeries() && !suppressHeroAutofocus) {
                focusTimer.start()
            }
            Qt.callLater(root.loadRequestRecommendations)
            Qt.callLater(root.tryRestorePendingReturnState)
        }
        function onWatchlistUpdateFailed(message) {
            watchlistToast.show(message)
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
        target: MediaRequests

        // The backend changed (Seerr configured or removed, or the Silo
        // account's request access): drop the other backend's shelf.
        function onAvailabilityChanged() {
            requestPendingTmdbId = ""
            requestLoadedTmdbId = ""
            requestRecommendedItems = []
            requestRecommendationsLoading = false
            if (MediaRequests.available)
                Qt.callLater(root.loadRequestRecommendations)
        }

        function onSimilarResultsLoaded(mediaType, requestedTmdbId, results) {
            const requestTmdbId = String(requestedTmdbId)
            if (String(mediaType).toLowerCase() !== similarMediaType
                    || requestTmdbId !== root.tmdbId
                    || requestTmdbId !== requestPendingTmdbId) {
                return
            }

            const mappedResults = []
            for (let i = 0; i < results.length; ++i) {
                mappedResults.push(results[i])
            }
            requestRecommendedItems = mappedResults
            if (root.requestRecommendationCacheStore) {
                root.requestRecommendationCacheStore[root.recommendationCacheKey(requestTmdbId)] = {
                    timestamp: Date.now(),
                    items: mappedResults.slice()
                }
            }
            requestLoadedTmdbId = requestTmdbId
            requestPendingTmdbId = ""
            requestRecommendationsLoading = false
            Qt.callLater(root.tryRestorePendingReturnState)
        }

        function onSimilarResultsFailed(mediaType, requestedTmdbId, error) {
            const requestTmdbId = String(requestedTmdbId)
            if (String(mediaType).toLowerCase() !== similarMediaType
                    || requestTmdbId !== root.tmdbId
                    || requestTmdbId !== requestPendingTmdbId) {
                return
            }

            console.warn("[SeriesDetailsView] Similar titles to request failed:", error)
            requestPendingTmdbId = ""
            requestRecommendationsLoading = false
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
            Qt.callLater(root.loadRequestRecommendations)
            Qt.callLater(root.tryRestorePendingReturnState)
        }
    }

    property var savedFocusItem: null
    property int savedSeasonIndex: -1
    property bool restoringFocusFromNavigation: false
    property bool restoringFocusFromReturnState: false
    property bool suppressHeroAutofocus: false

    function saveFocusForNavigation() {
        savedSeasonIndex = seasonsList.currentIndex
        savedFocusItem = root.Window.activeFocusItem
    }

    function restoreFocusFromNavigation() {
        restoringFocusFromNavigation = true
        Qt.callLater(root.restoreSavedSidebarFocus)
    }

    function restoreSavedSidebarFocus() {
        if (savedSeasonIndex >= 0 && seasonsList.count > 0) {
            seasonsList.currentIndex = Math.min(savedSeasonIndex, seasonsList.count - 1)
        }

        if (savedFocusItem && savedFocusItem.parent && typeof savedFocusItem.forceActiveFocus === "function") {
            savedFocusItem.forceActiveFocus()
        } else if (savedSeasonIndex >= 0 && seasonsList.count > 0) {
            seasonsList.forceActiveFocus()
        } else if (playButton.enabled) {
            playButton.forceActiveFocus()
        } else {
            root.focusFirstLowerSection()
        }

        savedFocusItem = null
        Qt.callLater(function() {
            restoringFocusFromNavigation = false
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
