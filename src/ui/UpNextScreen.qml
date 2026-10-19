import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BloomUI

/**
 * UpNextScreen - Post-playback interstitial showing the next episode
 *
 * Appears after an episode finishes (or hits the watched threshold).
 * When autoplay is enabled, a configurable countdown starts; when it
 * reaches zero the next episode plays automatically.  The user can:
 *   - Press Enter/Space on the thumbnail to play immediately
 *   - Press "More Episodes" (or Escape) to return to the episode list
 *   - Press "Back to Home" to return to the home screen
 *
 * All navigation is fully keyboard-accessible.
 */
FocusScope {
    id: root

    // ---- Properties set by Main.qml when the screen is pushed ----
    property var episodeData: ({})
    property string seriesId: ""
    property int lastAudioIndex: -1
    property int lastSubtitleIndex: -1
    property bool autoplay: false
    readonly property bool hasNextEpisode: episodeId !== "" && !episodeData.noNextEpisode
    
    // Tell Main.qml's global ESC shortcut to skip this screen
    readonly property bool handlesOwnBackNavigation: true

    // ---- Signals emitted back to Main.qml ----
    signal playRequested()
    signal moreEpisodesRequested()
    signal backToHomeRequested()
    signal recommendationSelected(var itemData)

    // ---- Derived episode metadata ----
    readonly property string episodeName: episodeData.name || ""
    readonly property string seriesName: episodeData.seriesName || ""
    readonly property int seasonNumber: episodeData.parentIndexNumber || 0
    readonly property int episodeNumber: episodeData.indexNumber || 0
    readonly property string overview: episodeData.overview || ""
    readonly property double durationMs: episodeData.durationMs || 0
    readonly property double communityRating: episodeData.communityRating || 0
    readonly property string premiereDate: episodeData.premiereDate || ""
    readonly property var recommendationItems: (typeof UpNextRecommendationsViewModel !== "undefined")
        ? UpNextRecommendationsViewModel.items : []
    readonly property bool hasRecommendations: !root.hasNextEpisode && recommendationItems.length > 0

    // ---- Thumbnail URL construction ----
    readonly property string episodeId: episodeData.itemId || ""
    readonly property string thumbnailUrl: artworkUrlFromRef(
                                                   episodeData.thumbArtwork
                                                   || episodeData.primaryArtwork
                                                   || episodeData.seriesPrimaryArtwork,
                                                   960)

    // ---- Countdown timer ----
    property int countdown: hasNextEpisode && autoplay
        ? Math.max(1, (typeof ConfigManager !== 'undefined' ? ConfigManager.autoplayCountdownSeconds : 10))
        : -1
    readonly property bool countdownActive: hasNextEpisode && autoplay && countdown > 0

    Timer {
        id: countdownTimer
        interval: 1000
        repeat: true
        running: root.countdownActive && root.visible
        onTriggered: {
            root.countdown--
            if (root.countdown <= 0) {
                countdownTimer.stop()
                root.playRequested()
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

    function formatRuntime(milliseconds) {
        if (!milliseconds || milliseconds <= 0) return ""
        const totalMinutes = Math.round(milliseconds / 60000)
        const hours = Math.floor(totalMinutes / 60)
        const minutes = totalMinutes % 60
        if (hours > 0) return hours + "h " + minutes + "m"
        return minutes + "m"
    }

    function formatRating(rating) {
        if (!rating || rating <= 0) return ""
        return "★ " + rating.toFixed(1)
    }

    function cancelCountdown() {
        countdownTimer.stop()
        root.countdown = -1
    }

    function returnToSeries() {
        cancelCountdown()
        moreEpisodesRequested()
    }

    function focusPrimaryAction() {
        Qt.callLater(function() {
            if (root.hasNextEpisode) {
                thumbnailFocus.forceActiveFocus()
            } else {
                moreEpisodesBtn.forceActiveFocus()
            }
        })
    }

    function loadRecommendationsIfNeeded() {
        if (root.hasNextEpisode || !root.seriesId
                || typeof UpNextRecommendationsViewModel === "undefined") {
            if (typeof UpNextRecommendationsViewModel !== "undefined") {
                UpNextRecommendationsViewModel.clear()
            }
            return
        }

        UpNextRecommendationsViewModel.loadForSeries(root.seriesId, 6)
    }

    function activateRecommendation(itemData, focusTarget) {
        cancelCountdown()
        if (itemData && itemData.source === "seerr") {
            seerrRequestDialog.openForItem(itemData, focusTarget)
            return
        }
        recommendationSelected(itemData)
    }

    onActiveFocusChanged: {
        if (activeFocus) {
            focusPrimaryAction()
        }
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            loadRecommendationsIfNeeded()
            focusPrimaryAction()
        } else if (StackView.status === StackView.Deactivating
                   && typeof UpNextRecommendationsViewModel !== "undefined") {
            UpNextRecommendationsViewModel.clear()
        }
    }

    onSeriesIdChanged: loadRecommendationsIfNeeded()
    onHasNextEpisodeChanged: loadRecommendationsIfNeeded()

    // ---- Background ----
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -Theme.navRailReservedWidth
        color: Theme.backgroundPrimary
    }

    // Blurred backdrop from series
    Image {
        id: backdropImage
        anchors.fill: parent
        anchors.leftMargin: -Theme.navRailReservedWidth
        source: thumbnailUrl
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        opacity: status === Image.Ready ? 0.55 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }

        layer.enabled: true
        layer.effect: MultiEffect {
            blurEnabled: true
            blur: 0.7
            blurMax: 64
        }
    }

    // Dark gradient overlay for readability
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -Theme.navRailReservedWidth
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.gradientOverlayStart }
            GradientStop { position: 0.4; color: Theme.gradientOverlayMiddle }
            GradientStop { position: 1.0; color: Theme.gradientOverlayEnd }
        }
    }

    // ---- Main layout ----
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMarginX
        anchors.rightMargin: Theme.pageMarginX
        anchors.topMargin: Theme.pageMarginTop
        anchors.bottomMargin: Theme.pageMarginTop
        spacing: Theme.spacingXLarge

        // Top: Countdown text
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(60 * Theme.layoutScale)

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: root.hasNextEpisode && root.autoplay && root.countdown > 0
                text: qsTr("Next episode in %1").arg(root.countdown)
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightStrong
                color: Theme.textSecondary
            }

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: root.hasNextEpisode && root.autoplay && root.countdown <= 0 && root.countdown !== -1
                text: qsTr("Starting…")
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightStrong
                color: Theme.accentPrimary
            }

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.hasNextEpisode || !root.autoplay || root.countdown === -1
                text: root.hasNextEpisode ? qsTr("Up Next") : qsTr("No Next Episode")
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
            }
        }

        // Centre: Thumbnail + Metadata
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingXLarge

            // Episode thumbnail card (focusable – acts as Play button)
            FocusScope {
                id: thumbnailFocus
                visible: root.hasNextEpisode
                Layout.preferredWidth: Math.min(root.width * 0.45, Math.round(800 * Theme.layoutScale))
                Layout.preferredHeight: Layout.preferredWidth * 9 / 16
                Layout.alignment: Qt.AlignVCenter
                focus: true  // default focus

                Keys.onReturnPressed: function(event) {
                    if (event.isAutoRepeat) { event.accepted = true; return }
                    root.cancelCountdown()
                    root.playRequested()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    if (event.isAutoRepeat) { event.accepted = true; return }
                    root.cancelCountdown()
                    root.playRequested()
                    event.accepted = true
                }
                Keys.onSpacePressed: function(event) {
                    if (event.isAutoRepeat) { event.accepted = true; return }
                    root.cancelCountdown()
                    root.playRequested()
                    event.accepted = true
                }

                KeyNavigation.down: backToHomeBtn
                KeyNavigation.right: backToHomeBtn

                Rectangle {
                    id: thumbnailCard
                    anchors.fill: parent
                    radius: Theme.imageRadius
                    color: Theme.artworkPlaceholder

                    FocusRing {
                        active: thumbnailFocus.activeFocus
                        radius: thumbnailCard.radius
                    }

                    Image {
                        id: thumbImage
                        anchors.fill: parent
                        anchors.margins: 0
                        source: root.thumbnailUrl
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        opacity: status === Image.Ready ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }

                        layer.enabled: true
                        layer.effect: MultiEffect {
                            maskEnabled: true
                            maskSource: thumbMask
                        }
                    }

                    Rectangle {
                        id: thumbMask
                        anchors.fill: parent
                        radius: Theme.imageRadius
                        visible: false
                        layer.enabled: true
                        layer.smooth: true
                    }

                    // Dark overlay + play icon
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.imageRadius
                        color: thumbnailFocus.activeFocus ? Qt.rgba(0, 0, 0, 0.15) : Qt.rgba(0, 0, 0, 0.35)
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }

                        // Play circle
                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.round(80 * Theme.layoutScale)
                            height: width
                            radius: width / 2
                            color: thumbnailFocus.activeFocus ? Theme.actionPrimaryBackground : Qt.rgba(0, 0, 0, 0.45)
                            border.width: Theme.borderWidth
                            border.color: Qt.rgba(1, 1, 1, 0.35)

                            Behavior on color { ColorAnimation { duration: Theme.durationShort } }

                            Text {
                                anchors.centerIn: parent
                                // Slight right offset for visual balance of play triangle
                                anchors.horizontalCenterOffset: Math.round(3 * Theme.layoutScale)
                                text: Icons.playArrow
                                font.family: Theme.fontIcon
                                font.pixelSize: Math.round(40 * Theme.layoutScale)
                                color: thumbnailFocus.activeFocus ? Theme.actionPrimaryText : Theme.textOnDarkOverlay
                            }
                        }
                    }

                    // Autoplay progress bar at bottom
                    Rectangle {
                        visible: root.autoplay && root.countdown > 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Math.round(2 * Theme.layoutScale)
                        anchors.rightMargin: Math.round(2 * Theme.layoutScale)
                        anchors.bottomMargin: Math.round(2 * Theme.layoutScale)
                        height: Theme.progressHeight
                        radius: height / 2
                        color: Theme.progressTrackOnArtwork

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            radius: height / 2
                            color: Theme.progressFill
                            width: {
                                var total = typeof ConfigManager !== 'undefined' ? ConfigManager.autoplayCountdownSeconds : 10
                                total = Math.max(1, total)
                                return parent.width * (1.0 - root.countdown / total)
                            }
                            Behavior on width { NumberAnimation { duration: 900; easing.type: Easing.Linear } }
                        }
                    }
                }

                // Mouse/touch play
                MouseArea {
                    anchors.fill: parent
                    cursorShape: InputModeManager.pointerActive ? Qt.PointingHandCursor : Qt.BlankCursor
                    onClicked: {
                        root.cancelCountdown()
                        root.playRequested()
                    }
                }
            }

            // Metadata column
            ColumnLayout {
                id: metadataColumn
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.spacingMedium

                // Series name
                Text {
                    visible: root.hasNextEpisode && root.seriesName !== ""
                    text: root.seriesName
                    font.pixelSize: Theme.fontSizeMedium
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightStrong
                    color: Theme.accentPrimary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                // Episode identifier
                Text {
                    visible: root.hasNextEpisode
                    text: {
                        var prefix = "S" + root.seasonNumber + " E" + root.episodeNumber
                        if (root.episodeNumber === 0) prefix = "Special"
                        if (root.episodeName) prefix += " · " + root.episodeName
                        return prefix
                    }
                    font.pixelSize: Theme.fontSizeTitle
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightHeavy
                    color: Theme.textPrimary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                // Metadata row: runtime, rating, year
                RowLayout {
                    visible: root.hasNextEpisode
                    spacing: Theme.spacingMedium

                    Text {
                        visible: root.durationMs > 0
                        text: root.formatRuntime(root.durationMs)
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                    }

                    Text {
                        visible: root.communityRating > 0
                        text: root.formatRating(root.communityRating)
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.warningColor
                    }

                    Text {
                        visible: root.premiereDate !== ""
                        text: {
                            if (!root.premiereDate) return ""
                            var d = new Date(root.premiereDate)
                            return isNaN(d.getTime()) ? "" : d.getFullYear().toString()
                        }
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                    }
                }

                // Overview
                Text {
                    visible: root.hasNextEpisode && root.overview !== ""
                    text: root.overview
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    lineHeight: 1.2
                    Layout.topMargin: Theme.spacingSmall
                }

                Text {
                    visible: !root.hasNextEpisode
                    text: qsTr("There isn't another unwatched episode available right now.")
                    font.pixelSize: Theme.fontSizeTitle
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightHeavy
                    color: Theme.textPrimary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    maximumLineCount: 3
                }

                Text {
                    visible: !root.hasNextEpisode
                    text: qsTr("You can return to the series or head back home.")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    maximumLineCount: 2
                }

                Item { Layout.fillHeight: true }
            }
        }

        // Bottom: Action buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeightLarge
            spacing: Theme.spacingLarge

            // "Back to Home" button
            ActionButton {
                id: backToHomeBtn
                variant: "secondary"
                text: qsTr("Back to Home")
                iconGlyph: Icons.home
                implicitHeight: Theme.buttonHeightLarge
                focusPolicy: Qt.StrongFocus

                KeyNavigation.up: root.hasNextEpisode ? thumbnailFocus : moreEpisodesBtn
                KeyNavigation.right: moreEpisodesBtn
                KeyNavigation.down: root.hasRecommendations ? recommendationsList : null

                Keys.onReturnPressed: function(event) {
                    if (event.isAutoRepeat) { event.accepted = true; return }
                    root.cancelCountdown()
                    root.backToHomeRequested()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    if (event.isAutoRepeat) { event.accepted = true; return }
                    root.cancelCountdown()
                    root.backToHomeRequested()
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) {
                    if (root.hasRecommendations) {
                        recommendationsList.forceActiveFocus()
                        recommendationsList.currentIndex = Math.max(0, recommendationsList.currentIndex)
                        event.accepted = true
                    }
                }

                onClicked: {
                    root.cancelCountdown()
                    root.backToHomeRequested()
                }


            }

            // "More Episodes" button
            ActionButton {
                id: moreEpisodesBtn
                variant: "secondary"
                text: root.hasNextEpisode ? qsTr("More Episodes") : qsTr("Back to Series")
                iconGlyph: Icons.viewList
                implicitHeight: Theme.buttonHeightLarge
                focusPolicy: Qt.StrongFocus

                KeyNavigation.up: root.hasNextEpisode ? thumbnailFocus : backToHomeBtn
                KeyNavigation.left: backToHomeBtn
                KeyNavigation.down: root.hasRecommendations ? recommendationsList : null

                Keys.onReturnPressed: function(event) {
                    if (event.isAutoRepeat) { event.accepted = true; return }
                    root.cancelCountdown()
                    root.moreEpisodesRequested()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    if (event.isAutoRepeat) { event.accepted = true; return }
                    root.cancelCountdown()
                    root.moreEpisodesRequested()
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) {
                    if (root.hasRecommendations) {
                        recommendationsList.forceActiveFocus()
                        recommendationsList.currentIndex = Math.max(0, recommendationsList.currentIndex)
                        event.accepted = true
                    }
                }

                onClicked: {
                    root.cancelCountdown()
                    root.moreEpisodesRequested()
                }


            }

            Item { Layout.fillWidth: true }
        }

        ColumnLayout {
            id: recommendationsSection
            Layout.fillWidth: true
            Layout.preferredHeight: root.hasRecommendations ? Math.round(300 * Theme.layoutScale) : 0
            spacing: Theme.spacingSmall
            visible: root.hasRecommendations

            Text {
                text: qsTr("Recommended Next")
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightStrong
                color: Theme.textPrimary
                Layout.fillWidth: true
            }

            ListView {
                id: recommendationsList
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: ListView.Horizontal
                spacing: Theme.spacingMedium
                clip: false
                focus: false
                currentIndex: 0
                model: root.recommendationItems
                boundsBehavior: Flickable.StopAtBounds

                delegate: FocusScope {
                    id: recommendationDelegateScope
                    width: Math.round(150 * Theme.layoutScale)
                    height: recommendationsList.height

                    required property var modelData
                    required property int index

                    SearchResultCard {
                        id: recommendationCard
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                        itemData: recommendationDelegateScope.modelData
                        isFocused: recommendationDelegateScope.activeFocus

                        onClicked: root.activateRecommendation(recommendationDelegateScope.modelData,
                                                               recommendationDelegateScope)
                    }

                    Keys.onReturnPressed: function(event) {
                        if (event.isAutoRepeat) { event.accepted = true; return }
                        recommendationCard.clicked()
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (event.isAutoRepeat) { event.accepted = true; return }
                        recommendationCard.clicked()
                        event.accepted = true
                    }
                    Keys.onSpacePressed: function(event) {
                        if (event.isAutoRepeat) { event.accepted = true; return }
                        recommendationCard.clicked()
                        event.accepted = true
                    }
                    Keys.onUpPressed: function(event) {
                        moreEpisodesBtn.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onLeftPressed: function(event) {
                        if (recommendationsList.currentIndex > 0) {
                            recommendationsList.currentIndex--
                            event.accepted = true
                        }
                    }
                    Keys.onRightPressed: function(event) {
                        if (recommendationsList.currentIndex + 1 < recommendationsList.count) {
                            recommendationsList.currentIndex++
                            event.accepted = true
                        }
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            recommendationsList.currentIndex = index
                        }
                    }
                }

                onActiveFocusChanged: {
                    if (activeFocus && currentItem) {
                        currentItem.forceActiveFocus()
                    }
                }

                onCurrentIndexChanged: {
                    if (activeFocus && currentItem) {
                        currentItem.forceActiveFocus()
                    }
                }
            }
        }
    }

    // ESC/Back cancels countdown and navigates to episode list or series.
    Keys.onEscapePressed: function(event) {
        returnToSeries()
        event.accepted = true
    }
    Keys.onBackPressed: function(event) {
        returnToSeries()
        event.accepted = true
    }

    // Initial focus
    Component.onCompleted: {
        loadRecommendationsIfNeeded()
        focusPrimaryAction()
    }

    SeerrRequestDialog {
        id: seerrRequestDialog
        parent: Overlay.overlay
    }
}
