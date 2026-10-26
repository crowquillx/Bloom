import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import BloomUI

/**
 * UpNextScreen - Post-playback interstitial showing the next episode
 *
 * Appears after an episode finishes (or hits the watched threshold). The
 * series backdrop and logo carry over from playback; the queued episode sits
 * on the left with a countdown ring when autoplay is enabled, and the rest of
 * the season runs along the bottom. The user can:
 *   - Press Play (or let the countdown finish) to play the queued episode
 *   - Pick another episode from the Up Next row to play it instead
 *   - Press Close, Escape, or Back to return to the episode list
 *
 * Main.qml keeps this screen up in its `starting` state until the new episode
 * is on screen, so playback never flashes back through Home.
 *
 * Without a next episode the same layout offers Back to Series, Home, and a
 * row of recommended series. All navigation is keyboard/gamepad accessible.
 */
FocusScope {
    id: root
    objectName: "upNextScreen"
    // Connection whose items this page shows; Main.qml stamps it when shown.
    property string ownerConnectionId: ""

    // ---- Properties set by Main.qml when the screen is pushed ----
    property var episodeData: ({})
    property string seriesId: ""
    property int lastAudioIndex: -1
    property int lastSubtitleIndex: -1
    property bool autoplay: false
    readonly property bool hasNextEpisode: episodeId !== "" && !episodeData.noNextEpisode

    // Set by Main.qml once a play request is accepted; input is ignored and
    // the countdown ring turns into a spinner until playback covers the screen.
    property bool starting: false
    property var startingEpisode: null

    // Tell Main.qml's global ESC shortcut to skip this screen
    readonly property bool handlesOwnBackNavigation: true

    // ---- Signals emitted back to Main.qml ----
    signal playRequested()
    signal episodeRequested(var episodeData)
    signal moreEpisodesRequested()
    signal backToHomeRequested()
    signal recommendationSelected(var itemData)

    readonly property var viewModel: typeof UpNextRecommendationsViewModel !== "undefined"
                                     ? UpNextRecommendationsViewModel : null
    readonly property var recommendationItems: viewModel ? viewModel.items : []
    readonly property var upcomingEpisodes: viewModel ? viewModel.upcomingEpisodes : []
    readonly property bool hasRecommendations: !root.hasNextEpisode && recommendationItems.length > 0

    // ---- Hero episode: the one starting, the one focused in the row, or the queued one ----
    readonly property string episodeId: episodeData.itemId || ""
    readonly property var previewEpisode: episodesList.activeFocus
                                          && episodesList.currentIndex >= 0
                                          && episodesList.currentIndex < upcomingEpisodes.length
                                          ? upcomingEpisodes[episodesList.currentIndex] : null
    readonly property var heroEpisode: startingEpisode || previewEpisode || episodeData
    readonly property bool heroIsQueued: (heroEpisode.itemId || "") === root.episodeId
    readonly property string seriesName: episodeData.seriesName || ""

    // ---- Artwork ----
    readonly property string logoUrl: (root.seriesId && typeof LibraryService !== "undefined")
                                      ? LibraryService.getCachedImageUrlWithWidth(root.seriesId, "Logo", 800)
                                      : ""
    readonly property string thumbnailUrl: artworkUrlFromRef(episodeData.thumbArtwork
                                                             || episodeData.primaryArtwork
                                                             || episodeData.seriesPrimaryArtwork,
                                                             1920)
    // Series backdrop first so the screen keeps one image while browsing the
    // row; the queued episode's still is the last resort.
    readonly property var backdropCandidates: {
        const candidates = []
        const fromRef = artworkUrlFromRef(episodeData.backdropArtwork, 1920)
        if (fromRef) candidates.push(fromRef)
        if (root.seriesId && typeof LibraryService !== "undefined") {
            const seriesBackdrop = LibraryService.getCachedImageUrlWithWidth(root.seriesId, "Backdrop", 1920)
            if (seriesBackdrop && candidates.indexOf(seriesBackdrop) < 0) candidates.push(seriesBackdrop)
        }
        if (root.thumbnailUrl && candidates.indexOf(root.thumbnailUrl) < 0) candidates.push(root.thumbnailUrl)
        return candidates
    }
    property int backdropCandidateIndex: 0
    onBackdropCandidatesChanged: backdropCandidateIndex = 0

    // ---- Countdown ----
    readonly property int countdownTotal: Math.max(1, typeof ConfigManager !== "undefined"
                                                   ? ConfigManager.autoplayCountdownSeconds : 10)
    property int countdown: hasNextEpisode && autoplay ? countdownTotal : -1
    readonly property bool countdownActive: hasNextEpisode && autoplay && countdown > 0 && !starting
    readonly property bool showCountdownSlot: countdownActive || (hasNextEpisode && starting)

    Timer {
        id: countdownTimer
        interval: 1000
        repeat: true
        running: root.countdownActive && root.visible
        onTriggered: {
            root.countdown--
            if (root.countdown <= 0) {
                countdownTimer.stop()
                root.playQueued()
            }
        }
    }

    // ---- Helper functions ----
    function artworkUrlFromRef(artwork, width) {
        if (!artwork || !artwork.itemId || typeof LibraryService === "undefined") {
            return ""
        }
        return LibraryService.getCachedArtworkUrlFromRef(artwork, width || 960)
    }

    function episodeLabel(episode) {
        if (!episode) return ""
        const number = episode.indexNumber || 0
        if (number === 0) return qsTr("Special")
        return qsTr("S%1 E%2").arg(episode.parentIndexNumber || 0).arg(number)
    }

    function episodeTitle(episode) {
        const label = episodeLabel(episode)
        return episode && episode.name ? label + "  •  " + episode.name : label
    }

    function cancelCountdown() {
        countdownTimer.stop()
        root.countdown = -1
    }

    function playQueued() {
        if (root.starting || !root.hasNextEpisode) return
        cancelCountdown()
        playRequested()
    }

    function playEpisode(episode) {
        if (root.starting || !episode || !episode.itemId) return
        if (episode.itemId === root.episodeId) {
            playQueued()
            return
        }
        cancelCountdown()
        episodeRequested(episode)
    }

    function returnToSeries() {
        if (root.starting) return
        cancelCountdown()
        moreEpisodesRequested()
    }

    function goHome() {
        if (root.starting) return
        cancelCountdown()
        backToHomeRequested()
    }

    /// Called by Main.qml when it accepts a play request.
    function beginStarting(episode) {
        cancelCountdown()
        root.startingEpisode = episode || root.episodeData
        root.starting = true
        primaryButton.forceActiveFocus()
    }

    function focusPrimaryAction() {
        Qt.callLater(function() {
            primaryButton.forceActiveFocus()
        })
    }

    // Property setup and completion all call this; only the first call for a
    // given context sends requests.
    property string rowsRequestKey: ""

    function loadRowsIfNeeded() {
        if (!root.viewModel) return
        const seasonId = root.episodeData.seasonId || root.episodeData.parentId || ""
        const key = root.hasNextEpisode ? "episodes:" + seasonId + ":" + root.episodeId
                                        : "recommendations:" + root.seriesId
        if (key === root.rowsRequestKey) return
        root.rowsRequestKey = key
        if (root.hasNextEpisode) {
            root.viewModel.clear()
            root.viewModel.loadUpcomingEpisodes(seasonId, root.episodeId, 8)
        } else {
            root.viewModel.clearUpcomingEpisodes()
            if (root.seriesId) {
                root.viewModel.loadForSeries(root.seriesId, 6)
            } else {
                root.viewModel.clear()
            }
        }
    }

    function activateRecommendation(itemData, focusTarget) {
        if (root.starting) return
        cancelCountdown()
        if (itemData && itemData.source === "seerr") {
            seerrRequestDialog.openForItem(itemData, focusTarget)
            return
        }
        recommendationSelected(itemData)
    }

    function activateOnKey(event, action) {
        if (!event.isAutoRepeat) action()
        event.accepted = true
    }

    onActiveFocusChanged: {
        if (activeFocus) {
            focusPrimaryAction()
        }
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            focusPrimaryAction()
        } else if (StackView.status === StackView.Deactivating && root.viewModel) {
            root.rowsRequestKey = ""
            root.viewModel.clear()
            root.viewModel.clearUpcomingEpisodes()
        }
    }

    onSeriesIdChanged: loadRowsIfNeeded()
    onHasNextEpisodeChanged: loadRowsIfNeeded()

    // ESC/Back cancels countdown and navigates to episode list or series.
    Keys.onEscapePressed: function(event) {
        returnToSeries()
        event.accepted = true
    }
    Keys.onBackPressed: function(event) {
        returnToSeries()
        event.accepted = true
    }

    Component.onCompleted: {
        loadRowsIfNeeded()
        focusPrimaryAction()
        if (Theme.uiAnimationsEnabled) {
            entrance.start()
        }
    }

    // ---- Backdrop ----
    // Opaque base matching Main's playback handoff surface, so the hop from
    // video to this screen never shows the page underneath.
    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundPrimary
    }

    Image {
        id: backdropImage
        anchors.fill: parent
        source: root.backdropCandidates.length > root.backdropCandidateIndex
                ? root.backdropCandidates[root.backdropCandidateIndex] : ""
        fillMode: Image.PreserveAspectCrop
        horizontalAlignment: Image.AlignRight
        asynchronous: true
        cache: true
        opacity: status === Image.Ready ? (root.hasNextEpisode ? 1.0 : 0.55) : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
        onStatusChanged: {
            if (status === Image.Error
                    && root.backdropCandidateIndex + 1 < root.backdropCandidates.length) {
                root.backdropCandidateIndex++
            }
        }
    }

    // Left scrim: solid behind the copy, clear over the right half of the art.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.scrimStrong }
            GradientStop { position: 0.4; color: Theme.scrimMedium }
            GradientStop { position: 0.72; color: Theme.scrimClear }
        }
    }

    // Bottom scrim under the card row.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.45
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.scrimClear }
            GradientStop { position: 0.55; color: Theme.scrimMedium }
            GradientStop { position: 1.0; color: Theme.scrimStrong }
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

        Column {
            id: heroColumn
            anchors.left: parent.left
            anchors.leftMargin: Theme.pageMarginX
            anchors.top: parent.top
            anchors.topMargin: Theme.pageMarginTop + Math.round(24 * Theme.layoutScale)
            width: Math.min(Math.round(parent.width * 0.42), Math.round(780 * Theme.layoutScale))
            spacing: Theme.spacingMedium
            transform: Translate { id: heroTranslate }

            // Series logo, or the series name when there is no logo.
            Item {
                width: parent.width
                height: Math.round(170 * Theme.layoutScale)

                ArtworkLogo {
                    id: logoImage
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(parent.width, Math.round(560 * Theme.layoutScale))
                    height: parent.height
                    source: root.logoUrl
                    horizontalAlignment: Image.AlignLeft
                    sourceSize.width: 800
                    visible: source !== "" && status !== Image.Error
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
                }

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !logoImage.visible
                    text: root.seriesName
                    font.pixelSize: Theme.fontSizeDisplay
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightHeavy
                    color: Theme.textPrimary
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }

            Item { width: 1; height: Theme.spacingSmall }

            Text {
                text: !root.hasNextEpisode ? qsTr("You're all caught up")
                      : (root.heroIsQueued ? qsTr("Next Episode") : qsTr("Episode"))
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightMedium
                color: Theme.textSecondary
            }

            Text {
                width: parent.width
                text: root.hasNextEpisode
                      ? root.episodeTitle(root.heroEpisode)
                      : qsTr("There isn't another unwatched episode available right now.")
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightStrong
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                readonly property string overviewText: root.hasNextEpisode
                                                       ? (root.heroEpisode.overview || "")
                                                       : qsTr("You can return to the series or head back home.")
                visible: overviewText !== ""
                text: overviewText
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 1.15
            }

            // Countdown ring, which becomes a spinner once playback is starting.
            Item {
                id: countdownSlot
                width: parent.width
                // Collapses smoothly when the countdown is cancelled.
                height: root.showCountdownSlot ? ring.height + Theme.spacingSmall : 0
                visible: height > 0
                clip: true
                Behavior on height { NumberAnimation { duration: Theme.durationLong; easing.type: Theme.easingStandard } }

                Item {
                    id: ring
                    readonly property real strokeWidth: Math.max(2, Math.round(5 * Theme.layoutScale))
                    readonly property real arcRadius: (width - strokeWidth) / 2
                    property real fraction: 1.0

                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    width: Math.round(92 * Theme.layoutScale)
                    height: width
                    opacity: root.countdownActive || root.starting ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }

                    NumberAnimation {
                        id: ringSweep
                        target: ring
                        property: "fraction"
                        from: 1.0
                        to: 0.0
                        duration: root.countdownTotal * 1000
                        running: root.countdownActive
                        paused: running && !root.visible
                    }

                    Shape {
                        id: ringShape
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer

                        ShapePath {
                            strokeColor: Theme.withAlpha(Theme.textPrimary, 0.16)
                            strokeWidth: ring.strokeWidth
                            fillColor: "transparent"
                            PathAngleArc {
                                centerX: ring.width / 2
                                centerY: ring.height / 2
                                radiusX: ring.arcRadius
                                radiusY: ring.arcRadius
                                startAngle: 0
                                sweepAngle: 360
                            }
                        }

                        ShapePath {
                            strokeColor: Theme.accentPrimary
                            strokeWidth: ring.strokeWidth
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            PathAngleArc {
                                centerX: ring.width / 2
                                centerY: ring.height / 2
                                radiusX: ring.arcRadius
                                radiusY: ring.arcRadius
                                startAngle: -90
                                sweepAngle: root.starting ? 90 : 360 * ring.fraction
                            }
                        }

                        RotationAnimator on rotation {
                            running: root.starting && Theme.uiAnimationsEnabled
                            from: 0
                            to: 360
                            duration: 1000
                            loops: Animation.Infinite
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !root.starting
                        text: Math.max(0, root.countdown)
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightMedium
                        color: Theme.textPrimary
                    }
                }

                Text {
                    anchors.left: ring.right
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.right: parent.right
                    anchors.verticalCenter: ring.verticalCenter
                    opacity: ring.opacity
                    text: root.starting
                          ? qsTr("Starting %1…").arg(root.episodeLabel(root.startingEpisode))
                          : (root.countdown === 1 ? qsTr("Next episode in 1 second")
                             : qsTr("Next episode in %1 seconds").arg(Math.max(0, root.countdown)))
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
            }

            Item { width: 1; height: Theme.spacingXSmall }

            Row {
                id: actionRow
                spacing: Theme.spacingMedium
                readonly property int iconButtonWidth: Math.round(206 * Theme.layoutScale)
                readonly property int iconGlyphSize: Math.round(44 * Theme.layoutScale)

                ActionButton {
                    id: primaryButton
                    variant: "primary"
                    implicitHeight: Theme.buttonHeightLarge
                    width: root.hasNextEpisode ? actionRow.iconButtonWidth : implicitWidth
                    text: root.hasNextEpisode ? "" : qsTr("Back to Series")
                    iconGlyph: root.hasNextEpisode ? Icons.playArrow : Icons.viewList
                    iconPixelSize: root.hasNextEpisode ? actionRow.iconGlyphSize : Theme.fontSizeIcon
                    accessibleLabel: root.hasNextEpisode
                                     ? qsTr("Play %1").arg(root.episodeTitle(root.episodeData)) : ""
                    focus: true

                    KeyNavigation.right: secondaryButton
                    Keys.onDownPressed: function(event) { event.accepted = cardRow.focusRow() }
                    Keys.onReturnPressed: function(event) { root.activateOnKey(event, primaryButton.activate) }
                    Keys.onEnterPressed: function(event) { root.activateOnKey(event, primaryButton.activate) }
                    onClicked: activate()

                    function activate() {
                        if (root.hasNextEpisode) root.playQueued()
                        else root.returnToSeries()
                    }
                }

                ActionButton {
                    id: secondaryButton
                    variant: "secondary"
                    implicitHeight: Theme.buttonHeightLarge
                    width: root.hasNextEpisode ? actionRow.iconButtonWidth : implicitWidth
                    text: root.hasNextEpisode ? "" : qsTr("Home")
                    iconGlyph: root.hasNextEpisode ? Icons.close : Icons.home
                    accessibleLabel: root.hasNextEpisode ? qsTr("Close") : ""
                    iconPixelSize: root.hasNextEpisode ? actionRow.iconGlyphSize : Theme.fontSizeIcon
                    toolTipText: root.hasNextEpisode ? qsTr("Back to episodes") : ""
                    enabled: !root.starting

                    KeyNavigation.left: primaryButton
                    Keys.onDownPressed: function(event) { event.accepted = cardRow.focusRow() }
                    Keys.onReturnPressed: function(event) { root.activateOnKey(event, secondaryButton.activate) }
                    Keys.onEnterPressed: function(event) { root.activateOnKey(event, secondaryButton.activate) }
                    onClicked: activate()

                    function activate() {
                        if (root.hasNextEpisode) root.returnToSeries()
                        else root.goHome()
                    }
                }
            }
        }

        // ---- Bottom row: rest of the season, or recommendations ----
        Item {
            id: cardRow
            readonly property bool hasCards: root.hasNextEpisode ? root.upcomingEpisodes.length > 0
                                                                 : root.hasRecommendations
            readonly property var activeList: root.hasNextEpisode ? episodesList : recommendationsList

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.pageMarginTop
            height: rowTitle.height + Theme.spacingSmall + activeList.height
            visible: hasCards
            opacity: hasCards ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.durationLong } }

            function focusRow() {
                if (!hasCards || root.starting) return false
                activeList.currentIndex = Math.max(0, activeList.currentIndex)
                activeList.forceActiveFocus()
                return true
            }

            Text {
                id: rowTitle
                anchors.left: parent.left
                anchors.leftMargin: Theme.pageMarginX
                text: root.hasNextEpisode ? qsTr("Up Next") : qsTr("Recommended Next")
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightMedium
                color: Theme.textPrimary
            }

            CardRowList {
                id: episodesList
                anchors.left: parent.left
                anchors.right: parent.right
                onUpRequested: primaryButton.forceActiveFocus()
                anchors.top: rowTitle.bottom
                anchors.topMargin: Theme.spacingSmall
                visible: root.hasNextEpisode
                cardWidth: Math.round(400 * Theme.layoutScale)
                cardHeight: Math.round(cardWidth * 9 / 16)
                model: root.hasNextEpisode ? root.upcomingEpisodes : []

                onActiveFocusChanged: {
                    if (activeFocus) root.cancelCountdown()
                }
                onActivated: function(index) { root.playEpisode(root.upcomingEpisodes[index]) }

                delegate: Item {
                    id: episodeCard
                    required property var modelData
                    required property int index
                    readonly property bool queued: (modelData.itemId || "") === root.episodeId
                    readonly property double progress: modelData.durationMs > 0 && modelData.positionMs > 0
                                                       ? Math.min(1, modelData.positionMs / modelData.durationMs) : 0

                    width: episodesList.cardWidth
                    height: episodesList.height

                    Accessible.role: Accessible.Button
                    Accessible.name: root.episodeTitle(modelData)
                    Accessible.focusable: true

                    Rectangle {
                        id: episodeFrame
                        anchors.centerIn: parent
                        width: episodesList.cardWidth
                        height: episodesList.cardHeight
                        radius: Theme.imageRadius
                        color: Theme.artworkPlaceholder
                        scale: episodeCard.activeFocus ? 1.0 : 0.97
                        Behavior on scale { NumberAnimation { duration: Theme.durationShort; easing.type: Theme.easingStandard } }

                        RoundedImage {
                            anchors.fill: parent
                            radius: parent.radius
                            source: root.artworkUrlFromRef(episodeCard.modelData.primaryArtwork
                                                           || episodeCard.modelData.thumbArtwork, 640)
                            sourceWidth: 640
                            sourceHeight: 360
                            opacity: ready ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: parent.height * 0.5
                            radius: parent.radius
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Theme.withAlpha(Theme.overlayTextBackground, 0.0) }
                                GradientStop { position: 1.0; color: Theme.withAlpha(Theme.overlayTextBackground, 0.85) }
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: progressTrack.visible ? progressTrack.top : parent.bottom
                            anchors.margins: Theme.spacingMedium
                            text: root.episodeLabel(episodeCard.modelData)
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            font.weight: Theme.fontWeightStrong
                            color: Theme.textOnDarkOverlay
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            id: progressTrack
                            visible: episodeCard.progress > 0
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: Theme.spacingMedium
                            height: Theme.progressHeight
                            radius: height / 2
                            color: Theme.progressTrackOnArtwork

                            Rectangle {
                                width: parent.width * episodeCard.progress
                                height: parent.height
                                radius: parent.radius
                                color: Theme.progressFill
                            }
                        }

                        // The queued episode keeps a selection outline while
                        // focus is elsewhere; focus draws the shared ring.
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: "transparent"
                            visible: episodeCard.queued && !episodeCard.activeFocus
                            border.width: Theme.selectionIndicatorWidth
                            border.color: Theme.selectionIndicator
                        }

                        FocusRing {
                            active: episodeCard.activeFocus
                            radius: parent.radius
                        }
                    }

                    MouseArea {
                        anchors.fill: episodeFrame
                        cursorShape: InputModeManager.pointerActive ? Qt.PointingHandCursor : Qt.BlankCursor
                        onClicked: root.playEpisode(episodeCard.modelData)
                    }
                }
            }

            CardRowList {
                id: recommendationsList
                anchors.left: parent.left
                anchors.right: parent.right
                onUpRequested: primaryButton.forceActiveFocus()
                anchors.top: rowTitle.bottom
                anchors.topMargin: Theme.spacingSmall
                visible: !root.hasNextEpisode
                cardWidth: Math.round(170 * Theme.layoutScale)
                cardHeight: Math.round(300 * Theme.layoutScale)
                model: root.hasNextEpisode ? [] : root.recommendationItems

                onActivated: function(index) {
                    if (currentItem) {
                        root.activateRecommendation(root.recommendationItems[index], currentItem)
                    }
                }

                delegate: FocusScope {
                    id: recommendationDelegate
                    required property var modelData
                    required property int index

                    width: recommendationsList.cardWidth
                    height: recommendationsList.height

                    SearchResultCard {
                        anchors.centerIn: parent
                        width: recommendationsList.cardWidth
                        height: recommendationsList.cardHeight
                        itemData: recommendationDelegate.modelData
                        isFocused: recommendationDelegate.activeFocus

                        onClicked: root.activateRecommendation(recommendationDelegate.modelData,
                                                               recommendationDelegate)
                    }
                }
            }
        }
    }

    /// Horizontal card row that bleeds to the right edge and scrolls with focus.
    component CardRowList: ListView {
        id: list
        property int cardWidth: 0
        property int cardHeight: 0
        /// Room above and below the cards for the focus ring.
        property int verticalInset: Theme.focusRingGap + Theme.focusRingWidth + Theme.spacingXSmall
        signal activated(int index)
        signal upRequested()

        function activateCurrent(event) {
            if (!event.isAutoRepeat && currentIndex >= 0) activated(currentIndex)
            event.accepted = true
        }

        height: cardHeight + verticalInset * 2
        orientation: ListView.Horizontal
        spacing: Theme.spacingMedium
        leftMargin: Theme.pageMarginX
        rightMargin: Theme.pageMarginX
        clip: true
        currentIndex: 0
        boundsBehavior: Flickable.StopAtBounds
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: Theme.pageMarginX
        preferredHighlightEnd: width - Theme.pageMarginX
        highlightMoveDuration: Theme.durationLong

        Keys.onUpPressed: function(event) {
            upRequested()
            event.accepted = true
        }
        Keys.onReturnPressed: function(event) { activateCurrent(event) }
        Keys.onEnterPressed: function(event) { activateCurrent(event) }
        Keys.onSpacePressed: function(event) { activateCurrent(event) }
    }

    SeerrRequestDialog {
        id: seerrRequestDialog
        parent: Overlay.overlay
    }
}
