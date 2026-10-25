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
    readonly property double durationMs: MovieDetailsViewModel.durationMs
    readonly property var communityRating: MovieDetailsViewModel.communityRating
    readonly property string premiereDate: MovieDetailsViewModel.premiereDate
    readonly property int productionYear: MovieDetailsViewModel.productionYear
    readonly property bool isPlayed: MovieDetailsViewModel.isWatched
    readonly property double positionMs: MovieDetailsViewModel.positionMs
    readonly property string officialRating: MovieDetailsViewModel.officialRating
    readonly property var genres: MovieDetailsViewModel.genres
    readonly property var castAndCrew: MovieDetailsViewModel.people || []
    readonly property var chapters: MovieDetailsViewModel.chapters || []
    readonly property bool chaptersLoading: MovieDetailsViewModel.chaptersLoading
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

    function artworkUrlFromRef(artwork, width) {
        if (!artwork || !artwork.itemId) {
            return ""
        }
        return LibraryService.getCachedArtworkUrlFromRef(artwork, width || 420)
    }

    component RecommendationPosterCard: Item {
        id: recommendationCard

        required property var itemData
        property bool isFocused: false
        property bool isHovered: InputModeManager.pointerActive && recommendationMouseArea.containsMouse
        readonly property string posterSource: root.artworkUrlFromRef(itemData.primaryArtwork, 420)
        readonly property string title: itemData.name || ""
        readonly property string subtitle: {
            if (itemData.productionYear) {
                return String(itemData.productionYear)
            }
            if (itemData.premiereDate) {
                const date = new Date(itemData.premiereDate)
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
            if (stream.type === "Video" && videoHdrLabel(stream) !== "") {
                return true
            }
        }
        return false
    }

    function primaryVideoStream() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return null
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Video") return stream
        }
        return null
    }

    function videoResolutionBadge() {
        var stream = primaryVideoStream()
        if (!stream) return ""
        var height = Number(stream.height || stream.Height || 0)
        var width = Number(stream.width || stream.Width || 0)
        if (height >= 2160 || width >= 3800) return qsTr("4K")
        if (height >= 1440) return qsTr("1440p")
        if (height >= 1080) return qsTr("1080p")
        if (height >= 720) return qsTr("720p")
        if (height > 0) return qsTr("%1p").arg(height)
        return ""
    }

    function videoHdrBadge() {
        return videoHdrLabel(primaryVideoStream())
    }

    function hasMediaBadges() {
        return videoResolutionBadge() !== "" || videoHdrBadge() !== ""
    }

    function videoHdrLabel(stream) {
        if (!stream) return ""
        var metadata = [
            stream.videoRange,
            stream.videoRangeType,
            stream.codecTag,
            stream.codecTagString,
            stream.codecId,
            stream.profile
        ].join(" ").toUpperCase()
        var dvProfile = Number(stream.dolbyVisionProfile || 0)
        if (dvProfile > 0 || metadata.indexOf("DOVI") >= 0 || metadata.indexOf("DOLBY VISION") >= 0
                || metadata.indexOf("DVHE") >= 0 || metadata.indexOf("DVH1") >= 0) {
            return qsTr("Dolby Vision")
        }
        if (metadata.indexOf("HDR10+") >= 0 || metadata.indexOf("HDR10PLUS") >= 0) {
            return qsTr("HDR10+")
        }
        if (metadata.indexOf("HLG") >= 0) {
            return qsTr("HLG")
        }
        var range = String(stream.videoRange || stream.videoRangeType || "").toUpperCase()
        if (range !== "" && range !== "SDR") {
            return metadata.indexOf("HDR10") >= 0 || range.indexOf("HDR") >= 0 ? qsTr("HDR10") : range
        }
        return ""
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

    function buildPlaybackRequest(startPositionOverride, restoreFocusTarget) {
        var framerate = getVideoFramerate()
        var hdr = isVideoHDR()
        var overlaySubtitle = productionYear > 0 ? String(productionYear) : ""
        var preferredAudioIndex = playbackInfo ? selectedAudioIndex : -2
        var preferredSubtitleIndex = playbackInfo ? selectedSubtitleIndex : -2

        return {
            itemId: movieId,
            startPositionMs: startPositionOverride !== undefined && startPositionOverride !== null
                             ? Math.round(startPositionOverride)
                             : Math.round(positionMs || 0),
            seriesId: "",
            seasonId: "",
            overlayTitle: movieName || qsTr("Now Playing"),
            overlaySubtitle: overlaySubtitle,
            overlayBackdropUrl: backdropUrl,
            overlayLogoUrl: logoUrl,
            preferredAudioIndex: preferredAudioIndex,
            preferredSubtitleIndex: preferredSubtitleIndex,
            isMovie: true,
            allowVersionPrompt: true,
            framerateHint: framerate,
            isHDRHint: hdr,
            restoreFocusTarget: restoreFocusTarget || playButton
        }
    }

    function startPlaybackWithTracks() {
        startPlaybackWithTracksAt(undefined, playButton)
    }

    function startPlaybackWithTracksAt(startPositionMs, restoreFocusTarget) {
        resetPlaybackReturnFocusState()

        if (playbackInfoLoading || !playbackInfo || !currentMediaSource) {
            if (!playbackInfoLoading && movieId !== "") {
                playbackInfoLoading = true
                PlaybackService.getPlaybackInfo(movieId)
            }
            playbackToast.show(qsTr("Playback is still preparing. Try again in a moment."))
            return
        }

        lastPlaybackRestoreFocusTarget = restoreFocusTarget || playButton
        playbackReturnFocusPending = true
        playbackReturnFocusActivated = false
        root.playRequested(buildPlaybackRequest(startPositionMs, lastPlaybackRestoreFocusTarget))
    }

    function formatRuntime(ms) {
        if (!ms || ms === 0) return ""
        var totalMinutes = Math.round(ms / 60000)
        var hours = Math.floor(totalMinutes / 60)
        var minutes = totalMinutes % 60
        if (hours > 0) {
            return qsTr("%1h %2m").arg(hours).arg(minutes)
        }
        return qsTr("%1m").arg(minutes)
    }

    function formatChapterTime(seconds) {
        var total = Math.max(0, Math.floor(seconds || 0))
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var remainder = total % 60
        function pad(value) { return value < 10 ? "0" + value : "" + value }
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(remainder)
                         : minutes + ":" + pad(remainder)
    }

    function calculateEndTime(ms) {
        if (!ms || ms === 0) return ""
        var now = new Date()
        var endTime = new Date(now.getTime() + ms)
        return qsTr("Ends %1").arg(endTime.toLocaleTimeString(Qt.locale(), "h:mm AP"))
    }

    function formatCommunityRating() {
        if (!communityRating || communityRating <= 0) return ""
        return "★ " + communityRating.toFixed(1)
    }

    function watchedMinutes() {
        return Math.round((positionMs || 0) / 60000)
    }

    function totalMinutes() {
        return Math.round((durationMs || 0) / 60000)
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
        if (itemIsDescendant(activeItem, chapterList)) {
            return "chapters"
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
        if (area === "chapters") return Math.max(0, chapterList.currentIndex)
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

        if (area === "chapters" && chapterSection.visible && chapterList.count > 0) {
            chapterList.currentIndex = Math.min(targetIndex, chapterList.count - 1)
            chapterList.positionViewAtIndex(chapterList.currentIndex, ListView.Contain)
            ensureItemVisible(chapterSection, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
            return focusCurrentViewItem(chapterList)
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
        const chaptersReady = chapterList.count > 0
        const recsReady = libraryRecommendationsList.count > 0
        const castPending = isLoading
        const chaptersPending = chaptersLoading
        const recsPending = libraryRecommendationsLoading

        if (desiredArea === "cast" && !castReady && !recsReady && (castPending || recsPending)) {
            return false
        }
        if (desiredArea === "chapters" && !chaptersReady && chaptersPending) {
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
        if (restoreFocusToArea("chapters", state.focusIndex || 0)) {
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
        if (chapterSection.visible && chapterList.count > 0) {
            chapterSection.focusCurrentOrFirst()
        } else if (castSection.visible && castList.count > 0) {
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

    onChaptersChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onChaptersLoadingChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
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
        visible: isLoading && movieName === ""
        z: 100
        kind: "loading"
        message: qsTr("Loading movie details…")
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

                        Item {
                            anchors.fill: parent

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

                            ArtworkPlaceholder {
                                anchors.fill: parent
                                visible: heroPosterImage.status !== Image.Ready
                                loading: heroPosterImage.status === Image.Loading
                                iconGlyph: Icons.movie
                                title: movieName
                            }

                            Row {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: Theme.spacingSmall
                                spacing: Theme.spacingXSmall
                                opacity: root.hasMediaBadges() ? 1.0 : 0.0

                                Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }

                                MetadataChip {
                                    text: root.videoResolutionBadge()
                                    onArtwork: true
                                }
                                MetadataChip {
                                    text: root.videoHdrBadge()
                                    onArtwork: true
                                }
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
                                text: movieName
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
                            items: [productionYear > 0 ? String(productionYear) : "", officialRating,
                                    formatRuntime(durationMs), calculateEndTime(durationMs), formatCommunityRating()]
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium
                            visible: movieRatingsRepeater.count > 0

                            Repeater {
                                id: movieRatingsRepeater
                                model: root.visibleExternalRatings()

                                RatingMetadataChip {
                                    required property var modelData
                                    ratingData: modelData
                                }
                            }
                        }

                        MetadataLine {
                            Layout.fillWidth: true
                            separator: "  \u00b7  "
                            color: Theme.textTertiary
                            items: genres || []
                        }

                        Item {
                            id: overviewContainer
                            Layout.fillWidth: true
                            Layout.preferredHeight: overviewColumn.implicitHeight

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
                                    text: movieOverview || qsTr("No description available.")
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: movieOverview ? Theme.textPrimary : Theme.textSecondary
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
                            Layout.bottomMargin: root.heroActionsBottomSpacing
                            spacing: Theme.spacingMedium

                            ActionButton {
                                id: playButton
                                variant: "primary"
                                text: positionMs > 0 ? qsTr("Resume") : qsTr("Play")
                                iconGlyph: Icons.playArrow
                                busy: playbackInfoLoading
                                enabled: movieId !== ""
                                implicitHeight: Theme.buttonHeightLarge
                                Layout.minimumWidth: Math.round(240 * Theme.layoutScale)

                                Accessible.name: positionMs > 0 ? qsTr("Resume") : qsTr("Play")

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

                                onClicked: root.startPlaybackWithTracks()
                            }

                            SecondaryActionButton {
                                id: markWatchedButton
                                text: isPlayed ? qsTr("Mark Unwatched") : qsTr("Mark Watched")
                                iconGlyph: isPlayed ? Icons.visibilityOff : Icons.visibility
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
                                iconGlyph: Icons.moreHoriz
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
                            visible: positionMs > 0 && !isPlayed
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
                                height: Theme.progressHeight
                                radius: height / 2
                                color: Theme.progressTrack

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: durationMs > 0 ? parent.width * Math.min(1, positionMs / durationMs) : 0
                                    radius: height / 2
                                    color: Theme.progressFill
                                }
                            }
                        }
                    }
                }
            }

            FocusScope {
                id: chapterSection
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                // Hide the whole section rather than showing an empty panel.
                visible: movieId !== "" && (chaptersLoading || chapters.length > 0)
                implicitHeight: chapterSectionContent.implicitHeight

                function focusCurrentOrFirst() {
                    if (chapterList.count <= 0) {
                        if (castList.count > 0) castSection.focusCurrentOrFirst()
                        return
                    }
                    chapterList.currentIndex = Math.max(0, chapterList.currentIndex)
                    chapterList.forceActiveFocus()
                    Qt.callLater(function() {
                        if (chapterList.currentItem) chapterList.currentItem.forceActiveFocus()
                    })
                }

                ColumnLayout {
                    id: chapterSectionContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: Theme.spacingMedium

                    Text {
                        text: qsTr("Chapters")
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(224 * Theme.layoutScale)
                        visible: chaptersLoading && chapters.length === 0
                        radius: Theme.radiusMedium
                        color: Theme.surfaceSunken
                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMedium
                            spacing: Theme.spacingMedium
                            Repeater {
                                model: 3
                                Rectangle {
                                    width: Math.round(248 * Theme.layoutScale)
                                    height: parent.height
                                    radius: Theme.radiusMedium
                                    color: Theme.surfaceInteractive
                                    opacity: 0.72
                                    SequentialAnimation on opacity {
                                        running: Theme.uiAnimationsEnabled && parent.visible
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.42; duration: Theme.durationNormal }
                                        NumberAnimation { to: 0.72; duration: Theme.durationNormal }
                                    }
                                }
                            }
                        }
                    }

                    ListView {
                        id: chapterList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(224 * Theme.layoutScale)
                        orientation: ListView.Horizontal
                        spacing: Theme.spacingMedium
                        model: chapters
                        visible: chapters.length > 0
                        clip: true
                        interactive: false
                        boundsBehavior: Flickable.StopAtBounds

                        onActiveFocusChanged: if (activeFocus) root.ensureItemVisible(chapterSection, Math.round(80 * Theme.layoutScale), Math.round(160 * Theme.layoutScale))
                        onCurrentIndexChanged: if (activeFocus && currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

                        delegate: FocusScope {
                            id: chapterDelegate
                            required property int index
                            required property var modelData
                            width: Math.round(248 * Theme.layoutScale)
                            height: chapterList.height
                            focus: chapterList.activeFocus && chapterList.currentIndex === index
                            Keys.onLeftPressed: (event) => { if (index > 0) { chapterList.currentIndex = index - 1; event.accepted = true } else event.accepted = false }
                            Keys.onRightPressed: (event) => { if (index + 1 < chapterList.count) { chapterList.currentIndex = index + 1; event.accepted = true } else event.accepted = false }
                            Keys.onUpPressed: { playButton.forceActiveFocus() }
                            Keys.onDownPressed: { if (castList.count > 0) castSection.focusCurrentOrFirst(); else root.focusTarget(root.nextSectionAfterCast()) }
                            Keys.onReturnPressed: (event) => { if (!event.isAutoRepeat) root.startPlaybackWithTracksAt(modelData.startMs || 0, chapterDelegate); event.accepted = true }
                            Keys.onEnterPressed: (event) => { if (!event.isAutoRepeat) root.startPlaybackWithTracksAt(modelData.startMs || 0, chapterDelegate); event.accepted = true }

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusMedium
                                color: Theme.cardBackground
                                FocusRing { inset: true; active: chapterDelegate.activeFocus; radius: parent.radius }
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall
                                    spacing: Theme.spacingSmall
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: Math.round(132 * Theme.layoutScale)
                                        radius: Theme.radiusMedium
                                        color: Theme.cardBackgroundHover
                                        clip: true
                                        Image {
                                            id: movieChapterThumbnail
                                            anchors.fill: parent
                                            source: modelData.thumbnailUrl || ""
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            visible: source.toString().length > 0 && status === Image.Ready
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            visible: movieChapterThumbnail.status !== Image.Ready
                                            text: Icons.movie
                                            font.family: Theme.fontIcon
                                            font.pixelSize: Math.round(32 * Theme.layoutScale)
                                            color: Theme.textSecondary
                                        }
                                    }
                                    Text { Layout.fillWidth: true; text: modelData.name || qsTr("Chapter"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; font.bold: true; color: Theme.textPrimary; elide: Text.ElideRight }
                                    Text { Layout.fillWidth: true; text: formatChapterTime((modelData.startMs || 0) / 1000); font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontPrimary; color: Theme.textSecondary }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(224 * Theme.layoutScale)
                        visible: !chaptersLoading && chapters.length === 0
                        radius: Theme.radiusMedium
                        color: Theme.surfaceSunken
                        Text { anchors.centerIn: parent; text: qsTr("No chapters available."); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: Theme.textSecondary }
                    }
                }

                WheelStepScroller { anchors.fill: parent; target: chapterList; orientation: Qt.Horizontal; stepPx: Math.round(248 * Theme.layoutScale) + Theme.spacingMedium }
            }

            FocusScope {
                id: castSection
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: movieId !== ""
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

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.peopleCardHeight + Math.round(16 * Theme.layoutScale)
                        visible: isLoading && castAndCrew.length === 0
                        radius: Theme.radiusMedium
                        color: Theme.surfaceSunken
                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMedium
                            spacing: Theme.spacingMedium
                            Repeater {
                                model: 4
                                Rectangle {
                                    width: root.peopleCardWidth
                                    height: root.peopleCardHeight
                                    radius: Theme.radiusMedium
                                    color: Theme.surfaceInteractive
                                    opacity: 0.72
                                    SequentialAnimation on opacity {
                                        running: Theme.uiAnimationsEnabled && parent.visible
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.42; duration: Theme.durationNormal }
                                        NumberAnimation { to: 0.72; duration: Theme.durationNormal }
                                    }
                                }
                            }
                        }
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
                                if (chapterList.count > 0) chapterSection.focusCurrentOrFirst()
                                else playButton.forceActiveFocus()
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

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.peopleCardHeight + Math.round(16 * Theme.layoutScale)
                        visible: !isLoading && castAndCrew.length === 0
                        radius: Theme.radiusMedium
                        color: Theme.surfaceSunken
                        Text { anchors.centerIn: parent; text: qsTr("No cast or crew listed."); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: Theme.textSecondary }
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
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(120 * Theme.layoutScale)
                        visible: libraryRecommendationsLoading && libraryRecommendations.length === 0
                        radius: Theme.radiusMedium
                        color: Theme.surfaceSunken

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

    BloomMenu {
        id: contextMenu



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

        BloomMenuItem {
            id: watchedMenuItem
            text: isPlayed ? qsTr("Mark as Unwatched") : qsTr("Mark as Watched")



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

        BloomMenu {
            id: audioMenu
            title: qsTr("Audio: %1").arg(root.selectedAudioSummary())
            enabled: getAudioStreams().length > 0


            Repeater {
                model: getAudioStreams()

                BloomMenuItem {
                    id: audioMenuItem
                    required property var modelData

                    text: TrackUtils.formatTrackName(modelData)
                    checkable: true
                    checked: modelData.index === selectedAudioIndex
                    indicator: Item {}

                    marked: audioMenuItem.checked


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

        BloomMenu {
            id: subtitleMenu
            title: qsTr("Subtitles: %1").arg(root.selectedSubtitleSummary())
            enabled: getSubtitleStreams().length > 0 || selectedSubtitleIndex === -1


            BloomMenuItem {
                text: qsTr("Off")
                checkable: true
                checked: selectedSubtitleIndex === -1
                indicator: Item {}



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

                BloomMenuItem {
                    id: subtitleMenuItem
                    required property var modelData

                    text: TrackUtils.formatTrackName(modelData)
                    checkable: true
                    checked: modelData.index === selectedSubtitleIndex
                    indicator: Item {}

                    marked: subtitleMenuItem.checked


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
