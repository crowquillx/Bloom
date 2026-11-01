import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Layouts
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
 *
 * After several autoplayed episodes in a row without input (see
 * PlayerController.stillWatchingPromptActive) the screen asks "Still
 * watching?" instead: no countdown, Continue plays the queued episode and
 * Stop (or Back) returns to the series.
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
    // "Still watching?" prompt: replaces the countdown and the season row.
    property bool stillWatching: false
    readonly property bool hasNextEpisode: episodeId !== "" && !episodeData.noNextEpisode
    readonly property bool stillWatchingPrompt: stillWatching && hasNextEpisode

    // Set by Main.qml once a play request is accepted; input is ignored and
    // the countdown ring turns into a spinner until playback covers the screen.
    property bool starting: false
    property var startingEpisode: null

    // Tell Main.qml's global ESC shortcut to skip this screen
    readonly property bool handlesOwnBackNavigation: true

    // ---- Signals emitted back to Main.qml ----
    // `startedByAutoplay` is true only when the countdown ran out.
    signal playRequested(bool startedByAutoplay)
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
    readonly property bool countsDown: hasNextEpisode && autoplay && !stillWatchingPrompt
    property int countdown: countsDown ? countdownTotal : -1
    readonly property bool countdownActive: countsDown && countdown > 0 && !starting
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
                root.playQueued(true)
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

    function playQueued(startedByAutoplay) {
        if (root.starting || !root.hasNextEpisode) return
        cancelCountdown()
        playRequested(startedByAutoplay === true)
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
        if (itemData && itemData.source === "request") {
            requestDialog.openForItem(itemData, focusTarget)
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

    // ---- Stage ----
    // Opaque stage matching Main's playback handoff surface, so the hop from
    // video to this screen never shows the page underneath. Without a next
    // episode the art is quieted behind the "caught up" copy.
    StageBackdrop {
        id: stage
        anchors.fill: parent
        mode: "hero"
        source: root.backdropCandidates.length > root.backdropCandidateIndex
                ? root.backdropCandidates[root.backdropCandidateIndex] : ""
        dim: root.hasNextEpisode ? 0 : 0.45
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

        // Hero block: at the gutter, below the navigation bar, a fixed reading
        // measure (about half a 16:9 screen).
        Column {
            id: heroColumn
            anchors.left: parent.left
            anchors.leftMargin: Theme.pageMarginX
            anchors.top: parent.top
            anchors.topMargin: Theme.pageMarginTop
            width: Math.min(Theme.heroCopyMaxWidth, parent.width - Theme.pageMarginX * 2)
            spacing: Theme.spacingMedium
            transform: Translate { id: heroTranslate }

            // Why this screen is here, set as an eyebrow.
            Text {
                width: parent.width
                text: !root.hasNextEpisode ? qsTr("You're all caught up")
                      : root.stillWatchingPrompt ? qsTr("Still watching?")
                      : (root.heroIsQueued ? qsTr("Next Episode") : qsTr("Episode"))
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Theme.eyebrowLetterSpacing
                color: Theme.accentPrimary
                elide: Text.ElideRight
            }

            // Series logo, or the series name in display type.
            Item {
                width: parent.width
                height: logoImage.visible ? Theme.detailViewLogoHeight : seriesTitle.implicitHeight
                visible: logoImage.visible || root.seriesName !== ""

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
                    id: seriesTitle
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !logoImage.visible
                    text: root.seriesName
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
                text: root.hasNextEpisode
                      ? root.episodeTitle(root.heroEpisode)
                      : qsTr("There isn't another unwatched episode available right now.")
                font.pixelSize: Theme.fontSizeMedium
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightStrong
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                readonly property string overviewText: root.stillWatchingPrompt
                                                       ? qsTr("Autoplay paused after several episodes in a row. Continue with the next episode, or stop here.")
                                                       : root.hasNextEpisode
                                                         ? (root.heroEpisode.overview || "")
                                                         : qsTr("You can return to the series or head back home.")
                visible: overviewText !== ""
                text: overviewText
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                opacity: 0.86
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                lineHeight: 1.12
            }

            // Countdown ring, which becomes a spinner once playback is starting.
            Item {
                id: countdownSlot
                width: parent.width
                // Collapses smoothly when the countdown is cancelled.
                height: root.showCountdownSlot ? ring.height + Theme.spacingSmall : 0
                visible: height > 0
                clip: true
                Behavior on height {
                    enabled: Theme.uiAnimationsEnabled
                    NumberAnimation { duration: Theme.durationLong; easing.type: Theme.easingStandard }
                }

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

            // One primary action, then the secondary one.
            Row {
                id: actionRow
                topPadding: Theme.spacingSmall
                spacing: Theme.controlSpacing

                ActionButton {
                    id: primaryButton
                    variant: "primary"
                    implicitHeight: Theme.buttonHeightHero
                    labelPixelSize: Theme.fontSizeMedium
                    iconPixelSize: Theme.iconSizeLarge
                    sidePadding: Math.round(40 * Theme.layoutScale)
                    text: root.stillWatchingPrompt ? qsTr("Continue")
                          : root.hasNextEpisode
                            ? qsTr("Play %1").arg(root.episodeLabel(root.episodeData))
                            : qsTr("Back to Series")
                    iconGlyph: root.hasNextEpisode ? Icons.playArrow : Icons.viewList
                    accessibleLabel: root.stillWatchingPrompt
                                     ? qsTr("Continue with %1").arg(root.episodeTitle(root.episodeData))
                                     : root.hasNextEpisode
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
                    implicitHeight: Theme.buttonHeightHero
                    labelPixelSize: Theme.fontSizeMedium
                    iconPixelSize: Theme.iconSizeLarge
                    sidePadding: Math.round(36 * Theme.layoutScale)
                    text: root.stillWatchingPrompt ? qsTr("Stop")
                          : root.hasNextEpisode ? "" : qsTr("Home")
                    iconGlyph: root.stillWatchingPrompt ? Icons.stop
                               : root.hasNextEpisode ? Icons.close : Icons.home
                    accessibleLabel: root.stillWatchingPrompt ? qsTr("Stop and return to the series")
                                     : root.hasNextEpisode ? qsTr("Close") : ""
                    toolTipText: root.hasNextEpisode && !root.stillWatchingPrompt ? qsTr("Back to episodes") : ""
                    enabled: !root.starting

                    KeyNavigation.left: primaryButton
                    Keys.onRightPressed: function(event) { event.accepted = true }
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

        // ---- Bottom shelf: rest of the season, or recommendations ----
        ColumnLayout {
            id: cardRow
            // The still-watching prompt offers only Continue and Stop.
            readonly property bool hasCards: root.stillWatchingPrompt ? false
                                             : root.hasNextEpisode ? root.upcomingEpisodes.length > 0
                                                                   : root.hasRecommendations
            readonly property var activeList: root.hasNextEpisode ? episodesList : recommendationsList

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.pageMarginBottom - Theme.shelfFocusMargin
            spacing: Theme.sectionTitleSpacing
            visible: hasCards
            opacity: hasCards ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.durationLong } }

            function focusRow() {
                if (!hasCards || root.starting) return false
                activeList.currentIndex = Math.max(0, activeList.currentIndex)
                activeList.forceActiveFocus()
                return true
            }

            SectionHeader {
                text: root.hasNextEpisode ? qsTr("Up Next") : qsTr("Recommended Next")
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            ShelfList {
                id: episodesList
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: root.hasNextEpisode
                cardHeight: Theme.landscapeCardHeight
                model: root.hasNextEpisode ? root.upcomingEpisodes : []

                onActiveFocusChanged: {
                    if (activeFocus) root.cancelCountdown()
                }

                Keys.onUpPressed: function(event) {
                    primaryButton.forceActiveFocus()
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) { event.accepted = true }
                Keys.onReturnPressed: function(event) { episodesList.activateCurrent(event) }
                Keys.onEnterPressed: function(event) { episodesList.activateCurrent(event) }
                Keys.onSpacePressed: function(event) { episodesList.activateCurrent(event) }

                function activateCurrent(event) {
                    if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.upcomingEpisodes.length)
                        root.playEpisode(root.upcomingEpisodes[currentIndex])
                    event.accepted = true
                }

                delegate: MediaCard {
                    id: episodeCard
                    required property var modelData
                    required property int index
                    readonly property bool queued: (modelData.itemId || "") === root.episodeId

                    variant: "landscape"
                    artworkSources: [root.artworkUrlFromRef(modelData.primaryArtwork || modelData.thumbArtwork, 640)]
                    title: modelData.name || ""
                    subtitle: root.episodeLabel(modelData)
                    placeholderIcon: Icons.tvShows
                    positionMs: modelData.positionMs || 0
                    durationMs: modelData.durationMs || 0
                    focused: ListView.isCurrentItem && ListView.view.activeFocus
                    Accessible.name: root.episodeTitle(modelData)
                    onClicked: root.playEpisode(modelData)

                    // The queued episode keeps a selection outline while
                    // focus is elsewhere; focus draws the shared ring.
                    Rectangle {
                        width: episodeCard.artworkWidth
                        height: episodeCard.artworkHeight
                        radius: Theme.radiusArtwork
                        color: "transparent"
                        border.width: Theme.selectionIndicatorWidth
                        border.color: Theme.selectionIndicator
                        visible: episodeCard.queued && !episodeCard.focused
                    }
                }
            }

            ShelfList {
                id: recommendationsList
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: !root.hasNextEpisode
                model: root.hasNextEpisode ? [] : root.recommendationItems

                Keys.onUpPressed: function(event) {
                    primaryButton.forceActiveFocus()
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) { event.accepted = true }
                Keys.onReturnPressed: function(event) { recommendationsList.activateCurrent(event) }
                Keys.onEnterPressed: function(event) { recommendationsList.activateCurrent(event) }
                Keys.onSpacePressed: function(event) { recommendationsList.activateCurrent(event) }

                function activateCurrent(event) {
                    if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.recommendationItems.length)
                        root.activateRecommendation(root.recommendationItems[currentIndex], recommendationsList)
                    event.accepted = true
                }

                delegate: MediaCard {
                    required property var modelData
                    required property int index
                    readonly property bool isRequest: modelData.source === "request"

                    variant: "poster"
                    artworkSources: [isRequest ? (modelData.imageUrl || "")
                                             : root.artworkUrlFromRef(modelData.primaryArtwork, 640)]
                    title: modelData.name || ""
                    subtitle: modelData.productionYear ? String(modelData.productionYear) : ""
                    placeholderIcon: Icons.tvShows
                    item: isRequest ? null : modelData
                    focused: ListView.isCurrentItem && ListView.view.activeFocus
                    onClicked: {
                        recommendationsList.currentIndex = index
                        recommendationsList.forceActiveFocus()
                        root.activateRecommendation(modelData, recommendationsList)
                    }
                }
            }
        }
    }

    MediaRequestDialog {
        id: requestDialog
        parent: Overlay.overlay
    }
}
