import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * AfterMovieScreen - post-playback interstitial after a movie
 *
 * Appears when a movie (not an extra) ends past the completion threshold;
 * PlayerController decides (PlayerPolicy::postPlaybackScreen) and Main.qml
 * pushes it on top of the page playback started from. It is the movie
 * counterpart of UpNextScreen and follows the same hero archetype: the
 * movie's backdrop and logo carry over from playback, a personal rating
 * control sits under the title, and "Because you watched" runs along the
 * bottom. There is no autoplay countdown. The user can:
 *   - Rate the movie (stars on Silo, like/dislike on Jellyfin)
 *   - Open a recommended movie or series
 *   - Press Done, Escape, or Back to return where they came from
 *
 * The display/HDR restore stays parked while this screen is up; every way
 * off it releases the restore. All navigation is keyboard/gamepad accessible.
 */
FocusScope {
    id: root
    objectName: "afterMovieScreen"
    // Connection whose items this page shows; Main.qml stamps it when shown.
    property string ownerConnectionId: ""

    // ---- Set by Main.qml from PlayerController::navigateToAfterMovie ----
    // itemId, connectionId, name, backdropUrl, logoUrl as playback had them.
    property var movieData: ({})

    // Tell Main.qml's global ESC shortcut to skip this screen
    readonly property bool handlesOwnBackNavigation: true

    signal doneRequested()
    signal recommendationSelected(var itemData)

    readonly property var viewModel: typeof UpNextRecommendationsViewModel !== "undefined"
                                     ? UpNextRecommendationsViewModel : null
    readonly property string movieId: movieData.itemId || ""
    // Fresh details once they arrive; playback's metadata until then.
    readonly property var details: viewModel && viewModel.movie && viewModel.movie.itemId === root.movieId
                                   ? viewModel.movie : ({})
    readonly property var ratingState: viewModel ? viewModel.movieRating : null
    readonly property var recommendationItems: viewModel ? viewModel.items : []
    readonly property bool hasRecommendations: recommendationItems.length > 0
    readonly property string movieName: details.name || movieData.name || ""

    // ---- Artwork ----
    readonly property string logoUrl: {
        if (root.movieId && typeof LibraryService !== "undefined")
            return LibraryService.getCachedImageUrlWithWidth(root.movieId, "Logo", 800)
        return movieData.logoUrl || ""
    }
    readonly property var backdropCandidates: {
        const candidates = []
        const fromRef = artworkUrlFromRef(details.backdropArtwork, 1920)
        if (fromRef) candidates.push(fromRef)
        if (movieData.backdropUrl && candidates.indexOf(movieData.backdropUrl) < 0)
            candidates.push(movieData.backdropUrl)
        if (root.movieId && typeof LibraryService !== "undefined") {
            const backdrop = LibraryService.getCachedImageUrlWithWidth(root.movieId, "Backdrop", 1920)
            if (backdrop && candidates.indexOf(backdrop) < 0) candidates.push(backdrop)
        }
        return candidates
    }
    property int backdropCandidateIndex: 0
    onBackdropCandidatesChanged: backdropCandidateIndex = 0

    readonly property string metadataText: {
        const parts = []
        if (details.productionYear) parts.push(String(details.productionYear))
        const minutes = Math.round((details.durationMs || 0) / 60000)
        if (minutes > 0) {
            const hours = Math.floor(minutes / 60)
            parts.push(hours > 0 ? qsTr("%1h %2m").arg(hours).arg(minutes % 60) : qsTr("%1m").arg(minutes))
        }
        if (details.officialRating) parts.push(details.officialRating)
        return parts.join("  •  ")
    }

    // ---- Helper functions ----
    function artworkUrlFromRef(artwork, width) {
        if (!artwork || !artwork.itemId || typeof LibraryService === "undefined") {
            return ""
        }
        return LibraryService.getCachedArtworkUrlFromRef(artwork, width || 960)
    }

    function releaseDisplay() {
        if (typeof PlayerController !== "undefined")
            PlayerController.releaseDeferredPostPlaybackDisplayRestore()
    }

    function done() {
        releaseDisplay()
        doneRequested()
    }

    function activateRecommendation(itemData) {
        if (!itemData || !itemData.itemId) return
        releaseDisplay()
        recommendationSelected(itemData)
    }

    function activateOnKey(event, action) {
        if (!event.isAutoRepeat) action()
        event.accepted = true
    }

    function focusPrimaryAction() {
        Qt.callLater(function() {
            doneButton.forceActiveFocus()
        })
    }

    property string loadedMovieId: ""

    function loadIfNeeded() {
        if (!root.viewModel || !root.movieId || root.movieId === root.loadedMovieId) return
        root.loadedMovieId = root.movieId
        root.viewModel.clearUpcomingEpisodes()
        root.viewModel.loadForMovie(root.movieId, 12)
    }

    onMovieIdChanged: loadIfNeeded()

    onActiveFocusChanged: {
        if (activeFocus) {
            focusPrimaryAction()
        }
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            focusPrimaryAction()
        } else if (StackView.status === StackView.Deactivating && root.viewModel) {
            root.loadedMovieId = ""
            root.viewModel.clear()
        }
    }

    // However the screen goes (Done, a recommendation, the navigation bar,
    // a space switch), the parked display restore must not stay parked.
    StackView.onRemoved: releaseDisplay()
    Component.onDestruction: releaseDisplay()

    Keys.onEscapePressed: function(event) {
        root.done()
        event.accepted = true
    }
    Keys.onBackPressed: function(event) {
        root.done()
        event.accepted = true
    }

    Component.onCompleted: {
        loadIfNeeded()
        focusPrimaryAction()
        if (Theme.uiAnimationsEnabled) {
            entrance.start()
        }
    }

    Connections {
        target: root.ratingState
        ignoreUnknownSignals: true
        function onUpdateFailed(message) {
            ratingToast.show(message)
        }
    }

    // ---- Stage ----
    // Opaque, like Up Next, so the hop from video never shows the page
    // underneath.
    StageBackdrop {
        id: stage
        anchors.fill: parent
        mode: "hero"
        source: root.backdropCandidates.length > root.backdropCandidateIndex
                ? root.backdropCandidates[root.backdropCandidateIndex] : ""
        onArtworkFailed: function(url) {
            if (url === source && root.backdropCandidateIndex + 1 < root.backdropCandidates.length) {
                root.backdropCandidateIndex++
            }
        }
    }

    // ---- Content ----
    Item {
        id: content
        anchors.fill: parent

        ParallelAnimation {
            id: entrance
            NumberAnimation {
                target: content; property: "opacity"
                from: 0; to: 1
                duration: Theme.durationLong; easing.type: Theme.easingStandard
            }
            NumberAnimation {
                target: heroTranslate; property: "y"
                from: Math.round(24 * Theme.layoutScale); to: 0
                duration: Theme.durationFade; easing.type: Theme.easingEmphasized
            }
        }

        // Hero block at the gutter, below the navigation bar.
        Column {
            id: actionColumn
            anchors.left: parent.left
            anchors.leftMargin: Theme.pageMarginX
            anchors.top: parent.top
            anchors.topMargin: Theme.pageMarginTop
            width: Math.min(Theme.heroCopyMaxWidth, parent.width - Theme.pageMarginX * 2)
            spacing: Theme.spacingMedium
            transform: Translate { id: heroTranslate }

            Text {
                width: parent.width
                text: qsTr("You Just Watched")
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Theme.eyebrowLetterSpacing
                color: Theme.accentPrimary
                elide: Text.ElideRight
            }

            // Movie logo, or the title in display type.
            Item {
                width: parent.width
                height: logoImage.visible ? Theme.detailViewLogoHeight : movieTitle.implicitHeight
                visible: logoImage.visible || root.movieName !== ""

                ArtworkLogo {
                    id: logoImage
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(parent.width, Theme.seriesLogoMaxWidth)
                    height: parent.height
                    source: root.logoUrl
                    horizontalAlignment: Image.AlignLeft
                    sourceSize.width: 800
                    visible: root.logoUrl !== "" && status !== Image.Error
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity {
                        enabled: Theme.uiAnimationsEnabled
                        NumberAnimation { duration: Theme.durationFade }
                    }
                }

                Text {
                    id: movieTitle
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !logoImage.visible
                    text: root.movieName
                    font.pixelSize: Theme.fontSizeHero
                    font.family: Theme.fontDisplay
                    font.weight: Theme.fontWeightHeavy
                    font.letterSpacing: Theme.displayLetterSpacing
                    color: Theme.textPrimary
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 0.95
                }
            }

            Text {
                width: parent.width
                visible: root.metadataText !== ""
                text: root.metadataText
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                opacity: 0.86
                elide: Text.ElideRight
            }

            // The viewer's rating; only where the provider has one.
            Column {
                width: parent.width
                spacing: Theme.spacingSmall
                visible: ratingControl.available
                topPadding: Theme.spacingSmall

                Text {
                    width: parent.width
                    text: ratingControl.value === 0 ? qsTr("How was it?")
                                                    : qsTr("Your rating: %1").arg(ratingControl.valueText)
                    font.pixelSize: Theme.fontSizeMedium
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightStrong
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }

                UserRatingControl {
                    id: ratingControl
                    ratingState: root.ratingState
                    // Up is left unhandled so it reaches the navigation bar.
                    Keys.onDownPressed: function(event) {
                        doneButton.forceActiveFocus()
                        event.accepted = true
                    }
                }
            }

            Row {
                id: actionRow
                topPadding: Theme.spacingSmall
                spacing: Theme.controlSpacing

                ActionButton {
                    id: doneButton
                    variant: "primary"
                    implicitHeight: Theme.buttonHeightHero
                    labelPixelSize: Theme.fontSizeMedium
                    iconPixelSize: Theme.iconSizeLarge
                    sidePadding: Math.round(40 * Theme.layoutScale)
                    text: qsTr("Done")
                    iconGlyph: Icons.check
                    focus: true

                    // Without a rating, Up reaches the navigation bar.
                    Keys.onUpPressed: function(event) {
                        event.accepted = ratingControl.available && ratingControl.focusCurrent()
                    }
                    Keys.onDownPressed: function(event) { event.accepted = cardRow.focusRow() }
                    Keys.onLeftPressed: function(event) { event.accepted = true }
                    Keys.onRightPressed: function(event) { event.accepted = true }
                    Keys.onReturnPressed: function(event) { root.activateOnKey(event, root.done) }
                    Keys.onEnterPressed: function(event) { root.activateOnKey(event, root.done) }
                    onClicked: root.done()
                }
            }
        }

        // ---- Bottom shelf: because you watched ----
        ColumnLayout {
            id: cardRow
            readonly property bool hasCards: root.hasRecommendations

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.pageMarginBottom - Theme.shelfFocusMargin
            spacing: Theme.sectionTitleSpacing
            visible: hasCards
            opacity: hasCards ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.durationLong } }

            function focusRow() {
                if (!hasCards) return false
                recommendationsList.currentIndex = Math.max(0, recommendationsList.currentIndex)
                recommendationsList.forceActiveFocus()
                return true
            }

            SectionHeader {
                text: root.movieName ? qsTr("Because You Watched %1").arg(root.movieName)
                                     : qsTr("Because You Watched")
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            ShelfList {
                id: recommendationsList
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                model: root.recommendationItems

                Keys.onUpPressed: function(event) {
                    doneButton.forceActiveFocus()
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) { event.accepted = true }
                Keys.onReturnPressed: function(event) { recommendationsList.activateCurrent(event) }
                Keys.onEnterPressed: function(event) { recommendationsList.activateCurrent(event) }
                Keys.onSpacePressed: function(event) { recommendationsList.activateCurrent(event) }

                function activateCurrent(event) {
                    if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.recommendationItems.length)
                        root.activateRecommendation(root.recommendationItems[currentIndex])
                    event.accepted = true
                }

                delegate: MediaCard {
                    id: recommendationCard
                    required property var modelData
                    required property int index

                    variant: "poster"
                    artworkSources: [root.artworkUrlFromRef(recommendationCard.modelData.primaryArtwork, 640)]
                    title: recommendationCard.modelData.name || ""
                    subtitle: recommendationCard.modelData.productionYear ? String(recommendationCard.modelData.productionYear) : ""
                    placeholderIcon: recommendationCard.modelData.mediaType === "Series" ? Icons.tvShows : Icons.movie
                    item: recommendationCard.modelData
                    focused: ListView.isCurrentItem && ListView.view.activeFocus
                    onClicked: {
                        recommendationsList.currentIndex = recommendationCard.index
                        recommendationsList.forceActiveFocus()
                        root.activateRecommendation(recommendationCard.modelData)
                    }
                }
            }
        }
    }

    ToastNotification {
        id: ratingToast
        z: 300
    }
}
