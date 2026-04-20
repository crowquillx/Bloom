import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import BloomUI
import "TrackUtils.js" as TrackUtils

FocusScope {
    id: root

    property string movieId: ""
    property var pendingReturnState: null
    property bool restorePendingReturnState: false
    readonly property bool isRestoringReturnFocus: restoringFocusFromSidebar
                                                 || restoringFocusFromReturnState
                                                 || suppressHeroAutofocus
                                                 || hasPendingReturnStateForCurrentMovie()

    readonly property string movieName: MovieDetailsViewModel.title
    readonly property string movieOverview: MovieDetailsViewModel.overview
    readonly property var runtimeTicks: MovieDetailsViewModel.runtimeTicks
    readonly property var communityRating: MovieDetailsViewModel.communityRating
    readonly property string premiereDate: MovieDetailsViewModel.premiereDate
    readonly property int productionYear: MovieDetailsViewModel.productionYear
    readonly property bool isPlayed: MovieDetailsViewModel.isWatched
    readonly property var playbackPositionTicks: MovieDetailsViewModel.playbackPositionTicks
    readonly property string officialRating: MovieDetailsViewModel.officialRating
    readonly property var genres: MovieDetailsViewModel.genres
    readonly property var castAndCrew: MovieDetailsViewModel.people || []
    readonly property var libraryRecommendations: MovieDetailsViewModel.similarItems || []
    readonly property bool libraryRecommendationsLoading: MovieDetailsViewModel.similarItemsLoading
    readonly property bool isLoading: MovieDetailsViewModel.isLoading
    readonly property var externalRatings: MovieDetailsViewModel.mdbListRatings || ({})

    readonly property string logoUrl: MovieDetailsViewModel.logoUrl
    readonly property string posterUrl: MovieDetailsViewModel.posterUrl
    readonly property string backdropUrl: MovieDetailsViewModel.backdropUrl

    readonly property int heroPosterWidth: Math.round(320 * Theme.layoutScale)
    readonly property int heroPosterHeight: Math.round(heroPosterWidth * 1.5)
    readonly property int heroPanelPadding: Theme.spacingXLarge
    readonly property int heroActionsBottomSpacing: Theme.spacingMedium
    readonly property int peopleCardWidth: Math.round(176 * Theme.layoutScale)
    readonly property int peopleCardHeight: Math.round(320 * Theme.layoutScale)
    readonly property int recommendationCardWidth: Math.round(236 * Theme.layoutScale)
    readonly property int recommendationCardHeight: recommendationCardWidth + Math.round(recommendationCardWidth * 0.5) + Math.round(74 * Theme.layoutScale)
    readonly property int shelfEdgePadding: Math.round(14 * Theme.layoutScale)

    property var playbackInfo: null
    property var currentMediaSource: playbackInfo && playbackInfo.mediaSources && playbackInfo.mediaSources.length > 0
                                     ? playbackInfo.mediaSources[0] : null
    property bool playbackInfoLoading: false
    property bool playbackReturnFocusPending: false
    property bool playbackReturnFocusActivated: false
    property Item lastPlaybackRestoreFocusTarget: null
    property int selectedAudioIndex: -1
    property int selectedSubtitleIndex: -1

    signal playRequested(var request)
    signal itemSelected(var itemData)
    signal backRequested()
    signal returnStateConsumed()

    component RecommendationPosterCard: Item {
        id: recommendationCard

        required property var itemData
        property bool isFocused: false
        property bool isHovered: InputModeManager.pointerActive && recommendationMouseArea.containsMouse
        readonly property string posterSource: itemData.Id
                                              ? LibraryService.getCachedImageUrlWithWidth(itemData.Id, "Primary", 420)
                                              : ""
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
                        text: recommendationCard.itemData.Type === "Series" ? Icons.tvShows : Icons.movie
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

    function getVideoFramerate() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return 0.0
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Video") {
                if (stream.realFrameRate && stream.realFrameRate > 0) return stream.realFrameRate
                if (stream.averageFrameRate && stream.averageFrameRate > 0) return stream.averageFrameRate
            }
        }
        return 0.0
    }

    function isVideoHDR() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return false
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Video" && stream.videoRange) {
                var range = String(stream.videoRange).toUpperCase()
                if (range !== "SDR" && range !== "") return true
            }
        }
        return false
    }

    function getAudioStreams() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return []
        var audio = []
        for (var i = 0; i < currentMediaSource.mediaStreams.length; ++i) {
            if (currentMediaSource.mediaStreams[i].type === "Audio") {
                audio.push(currentMediaSource.mediaStreams[i])
            }
        }
        return audio
    }

    function getSubtitleStreams() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return []
        var subtitles = []
        for (var i = 0; i < currentMediaSource.mediaStreams.length; ++i) {
            if (currentMediaSource.mediaStreams[i].type === "Subtitle") {
                subtitles.push(currentMediaSource.mediaStreams[i])
            }
        }
        return subtitles
    }

    function shortTrackLabel(track, fallback) {
        if (!track) {
            return fallback || qsTr("Default")
        }

        var title = track.title || track.Title
        if (title) {
            return title
        }

        var language = track.language || track.Language
        if (language) {
            return TrackUtils.getLanguageName(language)
        }

        var codec = track.codec || track.Codec
        if (codec) {
            return String(codec).toUpperCase()
        }

        var index = track.index !== undefined ? track.index : track.Index
        if (index !== undefined) {
            return qsTr("Track %1").arg(index)
        }

        return fallback || qsTr("Default")
    }

    function selectedAudioSummary() {
        var tracks = getAudioStreams()
        for (var i = 0; i < tracks.length; ++i) {
            if (tracks[i].index === selectedAudioIndex) {
                return shortTrackLabel(tracks[i], qsTr("Default"))
            }
        }
        return tracks.length > 0 ? shortTrackLabel(tracks[0], qsTr("Default")) : qsTr("Unavailable")
    }

    function selectedSubtitleSummary() {
        if (selectedSubtitleIndex === -1) {
            return qsTr("Off")
        }
        var tracks = getSubtitleStreams()
        for (var i = 0; i < tracks.length; ++i) {
            if (tracks[i].index === selectedSubtitleIndex) {
                return shortTrackLabel(tracks[i], qsTr("Off"))
            }
        }
        return tracks.length > 0 ? shortTrackLabel(tracks[0], qsTr("Auto")) : qsTr("Unavailable")
    }

    function buildPlaybackRequest() {
        var framerate = getVideoFramerate()
        var hdr = isVideoHDR()
        var overlaySubtitle = productionYear > 0 ? String(productionYear) : ""
        var preferredAudioIndex = playbackInfo ? selectedAudioIndex : -2
        var preferredSubtitleIndex = playbackInfo ? selectedSubtitleIndex : -2

        return {
            itemId: movieId,
            startPositionTicks: playbackPositionTicks || 0,
            seriesId: "",
            seasonId: "",
            overlayTitle: movieName || qsTr("Now Playing"),
            overlaySubtitle: overlaySubtitle,
            overlayBackdropUrl: backdropUrl,
            preferredAudioIndex: preferredAudioIndex,
            preferredSubtitleIndex: preferredSubtitleIndex,
            isMovie: true,
            allowVersionPrompt: true,
            framerateHint: framerate,
            isHDRHint: hdr,
            restoreFocusTarget: playButton
        }
    }

    function startPlaybackWithTracks() {
        resetPlaybackReturnFocusState()

        if (playbackInfoLoading || !playbackInfo || !currentMediaSource) {
            if (!playbackInfoLoading && movieId !== "") {
                playbackInfoLoading = true
                PlaybackService.getPlaybackInfo(movieId)
            }
            playbackToast.show(qsTr("Playback is still preparing. Try again in a moment."))
            return
        }

        lastPlaybackRestoreFocusTarget = playButton
        playbackReturnFocusPending = true
        playbackReturnFocusActivated = false
        root.playRequested(buildPlaybackRequest())
    }

    function formatRuntime(ticks) {
        if (!ticks || ticks === 0) return ""
        var totalMinutes = Math.round(ticks / 600000000)
        var hours = Math.floor(totalMinutes / 60)
        var minutes = totalMinutes % 60
        if (hours > 0) {
            return qsTr("%1h %2m").arg(hours).arg(minutes)
        }
        return qsTr("%1m").arg(minutes)
    }

    function calculateEndTime(ticks) {
        if (!ticks || ticks === 0) return ""
        var now = new Date()
        var runtimeMs = ticks / 10000
        var endTime = new Date(now.getTime() + runtimeMs)
        return qsTr("Ends %1").arg(endTime.toLocaleTimeString(Qt.locale(), "h:mm AP"))
    }

    function formatCommunityRating() {
        if (!communityRating || communityRating <= 0) return ""
        return "★ " + communityRating.toFixed(1)
    }

    function watchedMinutes() {
        return Math.round((playbackPositionTicks || 0) / 600000000)
    }

    function totalMinutes() {
        return Math.round((runtimeTicks || 0) / 600000000)
    }

    function remainingMinutes() {
        return Math.max(0, totalMinutes() - watchedMinutes())
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

    function hasPendingReturnStateForCurrentMovie() {
        return restorePendingReturnState
                && pendingReturnState
                && String(pendingReturnState.movieId || "") === String(movieId || "")
    }

    function currentFocusArea() {
        const activeItem = root.Window.activeFocusItem
        if (!activeItem) {
            return "hero"
        }
        if (itemIsDescendant(activeItem, playButton)
                || itemIsDescendant(activeItem, markWatchedButton)
                || itemIsDescendant(activeItem, contextMenuButton)) {
            return "hero"
        }
        if (itemIsDescendant(activeItem, castList)) {
            return "cast"
        }
        if (itemIsDescendant(activeItem, libraryRecommendationsList)) {
            return "libraryRecommendations"
        }
        return "hero"
    }

    function currentFocusIndex() {
        const area = currentFocusArea()
        const activeItem = root.Window.activeFocusItem

        if (area === "hero") {
            if (itemIsDescendant(activeItem, markWatchedButton)) return 1
            if (itemIsDescendant(activeItem, contextMenuButton)) return 2
            return 0
        }
        if (area === "cast") return Math.max(0, castList.currentIndex)
        if (area === "libraryRecommendations") return Math.max(0, libraryRecommendationsList.currentIndex)
        return 0
    }

    function saveReturnState() {
        return {
            movieId: movieId,
            focusArea: currentFocusArea(),
            focusIndex: currentFocusIndex(),
            contentY: contentFlickable.contentY
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
            if (targetIndex === 1 && markWatchedButton.visible) {
                markWatchedButton.forceActiveFocus()
            } else if (targetIndex === 2 && contextMenuButton.visible) {
                contextMenuButton.forceActiveFocus()
            } else {
                playButton.forceActiveFocus()
            }
            return true
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

        return false
    }

    function restoreReturnState(state) {
        if (!state || String(state.movieId || "") !== String(movieId || "")) {
            return false
        }

        if (state.contentY !== undefined && state.contentY >= 0) {
            const maxScroll = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
            contentFlickable.contentY = Math.min(state.contentY, maxScroll)
        }

        const desiredArea = state.focusArea || "hero"
        const castReady = castList.count > 0
        const recsReady = libraryRecommendationsList.count > 0
        const castPending = isLoading
        const recsPending = libraryRecommendationsLoading

        if (desiredArea === "cast" && !castReady && !recsReady && (castPending || recsPending)) {
            return false
        }
        if (desiredArea === "libraryRecommendations" && !recsReady && !castReady && (recsPending || castPending)) {
            return false
        }

        if (restoreFocusToArea(desiredArea, state.focusIndex || 0)) {
            return true
        }

        if (restoreFocusToArea("libraryRecommendations", state.focusIndex || 0)) {
            return true
        }

        if (restoreFocusToArea("cast", state.focusIndex || 0)) {
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
        if (!hasPendingReturnStateForCurrentMovie() || restoringFocusFromReturnState) {
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
        if (castSection.visible) {
            castSection.focusCurrentOrFirst()
        } else if (libraryRecommendationsSection.visible) {
            libraryRecommendationsSection.focusCurrentOrFirst()
        }
    }

    function resetPlaybackReturnFocusState() {
        playbackReturnFocusPending = false
        playbackReturnFocusActivated = false
        lastPlaybackRestoreFocusTarget = null
    }

    function restoreFocusAfterPlaybackExit() {
        if (!root.visible
                || !root.playbackReturnFocusPending
                || !root.playbackReturnFocusActivated
                || PlayerController.awaitingNextEpisodeResolution) {
            return
        }

        root.playbackReturnFocusPending = false
        root.playbackReturnFocusActivated = false

        root.forceActiveFocus()
        Qt.callLater(function() {
            if (lastPlaybackRestoreFocusTarget
                    && lastPlaybackRestoreFocusTarget.parent
                    && typeof lastPlaybackRestoreFocusTarget.forceActiveFocus === "function") {
                lastPlaybackRestoreFocusTarget.forceActiveFocus()
            } else if (playButton.enabled) {
                playButton.forceActiveFocus()
            } else {
                focusFirstLowerSection()
            }
        })
    }

    function nextSectionAfterCast() {
        if (libraryRecommendationsSection.visible) {
            return libraryRecommendationsSection
        }
        return null
    }

    Keys.onPressed: (event) => {
        if (event.isAutoRepeat) {
            event.accepted = true
            return
        }

        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            if (contextMenu.opened) {
                contextMenu.close()
                event.accepted = true
                return
            }
            root.backRequested()
            event.accepted = true
        }
    }

    focus: true

    onActiveFocusChanged: {
        if (!activeFocus) {
            return
        }
        if (restoringFocusFromSidebar
                || restoringFocusFromReturnState
                || suppressHeroAutofocus
                || hasPendingReturnStateForCurrentMovie()) {
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

    onMovieIdChanged: {
        if (movieId !== "") {
            MovieDetailsViewModel.loadMovieDetails(movieId)
            playbackInfoLoading = true
            playbackInfo = null
            selectedAudioIndex = -1
            selectedSubtitleIndex = -1
            PlaybackService.getPlaybackInfo(movieId)
        } else {
            MovieDetailsViewModel.clear()
            playbackInfo = null
            playbackInfoLoading = false
            selectedAudioIndex = -1
            selectedSubtitleIndex = -1
        }
        Qt.callLater(root.tryRestorePendingReturnState)
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
            text: qsTr("Loading movie details...")
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
                                text: Icons.movie
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
                                text: movieName
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

                            MetadataChip { text: productionYear > 0 ? String(productionYear) : "" }
                            MetadataChip { text: officialRating }
                            MetadataChip { text: formatRuntime(runtimeTicks) }
                            MetadataChip { text: calculateEndTime(runtimeTicks) }
                            MetadataChip { text: formatCommunityRating() }

                            Repeater {
                                model: root.visibleExternalRatings()

                                RatingMetadataChip {
                                    required property var modelData
                                    ratingData: modelData
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Repeater {
                                model: genres || []

                                MetadataChip {
                                    required property var modelData
                                    text: modelData
                                }
                            }
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
                                        text: movieOverview || qsTr("No description available.")
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
                                text: playbackPositionTicks > 0 ? qsTr("Resume") : qsTr("Play")
                                enabled: movieId !== ""
                                Layout.preferredHeight: Theme.buttonHeightLarge
                                Layout.preferredWidth: Theme.buttonHeightLarge

                                Accessible.name: text

                                KeyNavigation.right: markWatchedButton

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

                                onClicked: root.startPlaybackWithTracks()

                                ToolTip.visible: hovered && enabled
                                ToolTip.text: text
                                ToolTip.delay: 500

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

                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: Icons.playArrow
                                    font.family: Theme.fontIcon
                                    font.pixelSize: Theme.fontSizeIcon
                                    color: Theme.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            SecondaryActionButton {
                                id: markWatchedButton
                                text: isPlayed ? qsTr("Mark Unwatched") : qsTr("Mark Watched")
                                iconGlyph: isPlayed ? Icons.visibilityOff : Icons.visibility
                                showLabel: false
                                Layout.preferredHeight: Theme.buttonHeightLarge
                                Layout.preferredWidth: Theme.buttonHeightLarge

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

                                onClicked: {
                                    if (isPlayed) {
                                        MovieDetailsViewModel.markAsUnwatched()
                                    } else {
                                        MovieDetailsViewModel.markAsWatched()
                                    }
                                }
                            }

                            SecondaryActionButton {
                                id: contextMenuButton
                                text: ""
                                iconGlyph: Icons.moreVert
                                accessibleLabel: qsTr("More options")
                                Layout.preferredHeight: Theme.buttonHeightLarge
                                Layout.preferredWidth: Theme.buttonHeightLarge

                                KeyNavigation.left: markWatchedButton

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
                            }

                            Item { Layout.fillWidth: true }
                        }

                        ColumnLayout {
                            visible: playbackPositionTicks > 0 && !isPlayed
                            Layout.fillWidth: true
                            spacing: Math.round(4 * Theme.layoutScale)

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: qsTr("%1 of %2 min watched").arg(root.watchedMinutes()).arg(root.totalMinutes())
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textSecondary
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: qsTr("%1 min remaining").arg(root.remainingMinutes())
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textSecondary
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: Math.round(6 * Theme.layoutScale)
                                radius: Math.round(3 * Theme.layoutScale)
                                color: Qt.rgba(1, 1, 1, 0.2)

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: runtimeTicks > 0 ? parent.width * Math.min(1, playbackPositionTicks / runtimeTicks) : 0
                                    radius: 3
                                    color: Theme.accentPrimary
                                }
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
                                playButton.forceActiveFocus()
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
                                    playButton.forceActiveFocus()
                                }
                            }

                            Keys.onDownPressed: (event) => {
                                event.accepted = true
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
        }
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
            if (currentMediaSource) {
                var resolved = PlayerController.resolveTrackSelectionForMediaSource(currentMediaSource, root.movieId, true)
                selectedAudioIndex = resolved.audioIndex
                selectedSubtitleIndex = resolved.subtitleIndex
            }
        }

        onClosed: {
            Qt.callLater(function() {
                contextMenuButton.forceActiveFocus()
            })
        }

        MenuItem {
            id: watchedMenuItem
            text: isPlayed ? qsTr("Mark as Unwatched") : qsTr("Mark as Watched")

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
                if (isPlayed) {
                    MovieDetailsViewModel.markAsUnwatched()
                } else {
                    MovieDetailsViewModel.markAsWatched()
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
            id: audioMenu
            title: qsTr("Audio: %1").arg(root.selectedAudioSummary())
            enabled: getAudioStreams().length > 0

            background: Rectangle {
                implicitWidth: Math.round(280 * Theme.layoutScale)
                color: Theme.cardBackground
                radius: Theme.radiusMedium
                border.color: Theme.cardBorder
                border.width: 1
            }

            Repeater {
                model: getAudioStreams()

                MenuItem {
                    id: audioMenuItem
                    required property var modelData

                    text: TrackUtils.formatTrackName(modelData)
                    checkable: true
                    checked: modelData.index === selectedAudioIndex
                    indicator: Item {}

                    contentItem: RowLayout {
                        spacing: Theme.spacingSmall

                        Text {
                            text: audioMenuItem.checked ? "✓" : "  "
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.accentPrimary
                            Layout.preferredWidth: Math.round(20 * Theme.layoutScale)
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: scrollingAudioText.height
                            clip: true

                            Text {
                                id: scrollingAudioText
                                text: audioMenuItem.text
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary

                                SequentialAnimation on x {
                                    running: scrollingAudioText.width > parent.width && audioMenuItem.highlighted
                                    loops: Animation.Infinite

                                    PauseAnimation { duration: 1000 }
                                    NumberAnimation {
                                        to: -scrollingAudioText.width + parent.width
                                        duration: Math.max(1000, (scrollingAudioText.width - parent.width) * 20)
                                        easing.type: Easing.Linear
                                    }
                                    PauseAnimation { duration: 1000 }
                                    NumberAnimation {
                                        to: 0
                                        duration: Math.max(1000, (scrollingAudioText.width - parent.width) * 20)
                                        easing.type: Easing.Linear
                                    }
                                }

                                onXChanged: {
                                    if (!running && x !== 0) x = 0
                                }
                                property bool running: scrollingAudioText.width > parent.width && audioMenuItem.highlighted
                            }
                        }
                    }

                    background: Rectangle {
                        color: parent.highlighted ? Theme.hoverOverlay : "transparent"
                        radius: Theme.radiusSmall
                    }

                    onTriggered: {
                        selectedAudioIndex = modelData.index
                        if (root.movieId) {
                            PlayerController.setExplicitMovieAudioPreference(root.movieId, selectedAudioIndex)
                        }
                        contextMenu.close()
                    }
                }
            }
        }

        Menu {
            id: subtitleMenu
            title: qsTr("Subtitles: %1").arg(root.selectedSubtitleSummary())
            enabled: getSubtitleStreams().length > 0 || selectedSubtitleIndex === -1

            background: Rectangle {
                implicitWidth: Math.round(280 * Theme.layoutScale)
                color: Theme.cardBackground
                radius: Theme.radiusMedium
                border.color: Theme.cardBorder
                border.width: 1
            }

            MenuItem {
                text: qsTr("Off")
                checkable: true
                checked: selectedSubtitleIndex === -1
                indicator: Item {}

                contentItem: RowLayout {
                    spacing: Theme.spacingSmall

                    Text {
                        text: parent.checked ? "✓" : "  "
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.accentPrimary
                        Layout.preferredWidth: Math.round(20 * Theme.layoutScale)
                    }

                    Text {
                        text: parent.text
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
                    selectedSubtitleIndex = -1
                    if (root.movieId) {
                        PlayerController.setExplicitMovieSubtitlePreference(root.movieId, selectedSubtitleIndex)
                    }
                    contextMenu.close()
                }
            }

            Repeater {
                model: getSubtitleStreams()

                MenuItem {
                    id: subtitleMenuItem
                    required property var modelData

                    text: TrackUtils.formatTrackName(modelData)
                    checkable: true
                    checked: modelData.index === selectedSubtitleIndex
                    indicator: Item {}

                    contentItem: RowLayout {
                        spacing: Theme.spacingSmall

                        Text {
                            text: subtitleMenuItem.checked ? "✓" : "  "
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.accentPrimary
                            Layout.preferredWidth: Math.round(20 * Theme.layoutScale)
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: scrollingSubtitleText.height
                            clip: true

                            Text {
                                id: scrollingSubtitleText
                                text: subtitleMenuItem.text
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary

                                SequentialAnimation on x {
                                    running: scrollingSubtitleText.width > parent.width && subtitleMenuItem.highlighted
                                    loops: Animation.Infinite

                                    PauseAnimation { duration: 1000 }
                                    NumberAnimation {
                                        to: -scrollingSubtitleText.width + parent.width
                                        duration: Math.max(1000, (scrollingSubtitleText.width - parent.width) * 20)
                                        easing.type: Easing.Linear
                                    }
                                    PauseAnimation { duration: 1000 }
                                    NumberAnimation {
                                        to: 0
                                        duration: Math.max(1000, (scrollingSubtitleText.width - parent.width) * 20)
                                        easing.type: Easing.Linear
                                    }
                                }

                                onXChanged: {
                                    if (!running && x !== 0) x = 0
                                }
                                property bool running: scrollingSubtitleText.width > parent.width && subtitleMenuItem.highlighted
                            }
                        }
                    }

                    background: Rectangle {
                        color: parent.highlighted ? Theme.hoverOverlay : "transparent"
                        radius: Theme.radiusSmall
                    }

                    onTriggered: {
                        selectedSubtitleIndex = modelData.index
                        if (root.movieId) {
                            PlayerController.setExplicitMovieSubtitlePreference(root.movieId, selectedSubtitleIndex)
                        }
                        contextMenu.close()
                    }
                }
            }
        }
    }

    Connections {
        target: PlaybackService

        function onPlaybackInfoLoaded(itemId, info) {
            if (itemId === root.movieId) {
                root.playbackInfo = info
                root.playbackInfoLoading = false

                if (info.mediaSources && info.mediaSources.length > 0) {
                    var source = info.mediaSources[0]
                    var resolved = PlayerController.resolveTrackSelectionForMediaSource(source, root.movieId, true)
                    root.selectedAudioIndex = resolved.audioIndex
                    root.selectedSubtitleIndex = resolved.subtitleIndex
                }
            }
        }

        function onErrorOccurred(endpoint, error) {
            if (endpoint !== "getPlaybackInfo") {
                return
            }

            root.playbackInfoLoading = false
            root.resetPlaybackReturnFocusState()
        }
    }

    Connections {
        target: PlayerController

        function onIsPlaybackActiveChanged() {
            if (PlayerController.isPlaybackActive) {
                if (root.playbackReturnFocusPending) {
                    root.playbackReturnFocusActivated = true
                }
                return
            }

            if (!root.playbackReturnFocusPending
                    || !root.playbackReturnFocusActivated
                    || PlayerController.awaitingNextEpisodeResolution) {
                return
            }

            Qt.callLater(root.restoreFocusAfterPlaybackExit)
        }

        function onAwaitingNextEpisodeResolutionChanged() {
            if (PlayerController.awaitingNextEpisodeResolution) {
                root.resetPlaybackReturnFocusState()
            }
        }
    }

    Timer {
        id: focusTimer
        interval: 50
        repeat: false
        onTriggered: {
            if (playButton && playButton.enabled) {
                playButton.forceActiveFocus()
            } else {
                focusFirstLowerSection()
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

    ToastNotification {
        id: playbackToast
        z: 300
    }

    Connections {
        target: MovieDetailsViewModel
        function onMovieLoaded() {
            if (!hasPendingReturnStateForCurrentMovie() && !suppressHeroAutofocus) {
                focusTimer.start()
            }
            Qt.callLater(root.tryRestorePendingReturnState)
        }
    }

    Component.onCompleted: {
        if (movieId) {
            MovieDetailsViewModel.loadMovieDetails(movieId)
            playbackInfoLoading = true
            PlaybackService.getPlaybackInfo(movieId)
            Qt.callLater(root.tryRestorePendingReturnState)
        }
    }

    property var savedFocusItem: null
    property string savedFocusArea: "hero"
    property int savedFocusIndex: 0
    property bool restoringFocusFromSidebar: false
    property bool restoringFocusFromReturnState: false
    property bool suppressHeroAutofocus: false

    function saveFocusForSidebar() {
        savedFocusItem = root.Window.activeFocusItem
        savedFocusArea = currentFocusArea()
        savedFocusIndex = currentFocusIndex()
    }

    function restoreFocusFromSidebar() {
        restoringFocusFromSidebar = true
        Qt.callLater(root.restoreSavedSidebarFocus)
    }

    function restoreSavedSidebarFocus() {
        if (savedFocusItem && savedFocusItem.parent && typeof savedFocusItem.forceActiveFocus === "function") {
            savedFocusItem.forceActiveFocus()
        } else if (restoreFocusToArea(savedFocusArea, savedFocusIndex)) {
        } else if (playButton.enabled) {
            playButton.forceActiveFocus()
        } else {
            root.focusFirstLowerSection()
        }

        savedFocusItem = null
        savedFocusArea = "hero"
        savedFocusIndex = 0
        Qt.callLater(function() {
            restoringFocusFromSidebar = false
        })
    }
}
