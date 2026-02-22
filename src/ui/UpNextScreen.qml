import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

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
    
    // Tell Main.qml's global ESC shortcut to skip this screen
    readonly property bool handlesOwnBackNavigation: true

    // ---- Signals emitted back to Main.qml ----
    signal playRequested()
    signal moreEpisodesRequested()
    signal backToHomeRequested()

    // ---- Derived episode metadata ----
    readonly property string episodeName: episodeData.Name || ""
    readonly property string seriesName: episodeData.SeriesName || ""
    readonly property int seasonNumber: episodeData.ParentIndexNumber || 0
    readonly property int episodeNumber: episodeData.IndexNumber || 0
    readonly property string overview: episodeData.Overview || ""
    readonly property var runtimeTicks: episodeData.RunTimeTicks || 0
    readonly property double communityRating: episodeData.CommunityRating || 0
    readonly property string premiereDate: episodeData.PremiereDate || ""

    // ---- Thumbnail URL construction ----
    readonly property string episodeId: episodeData.Id || ""
    readonly property string thumbnailUrl: {
        if (!episodeId || typeof LibraryService === 'undefined') return ""
        // Try episode thumb, then primary, then series primary
        var imageTags = episodeData.ImageTags || {}
        if (imageTags.Thumb)
            return LibraryService.getCachedImageUrlWithWidth(episodeId, "Thumb", 960)
        if (imageTags.Primary)
            return LibraryService.getCachedImageUrlWithWidth(episodeId, "Primary", 960)
        // Fallback to series primary
        var seriesPrimaryTag = episodeData.SeriesPrimaryImageTag || ""
        var fallbackSeriesId = episodeData.SeriesId || seriesId
        if (seriesPrimaryTag && fallbackSeriesId)
            return LibraryService.getCachedImageUrlWithWidth(fallbackSeriesId, "Primary", 960)
        return ""
    }

    // ---- Countdown timer ----
    property int countdown: autoplay ? (typeof ConfigManager !== 'undefined' ? ConfigManager.autoplayCountdownSeconds : 10) : -1
    readonly property bool countdownActive: autoplay && countdown > 0

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
    function formatRuntime(ticks) {
        if (!ticks || ticks <= 0) return ""
        var totalMinutes = Math.round(ticks / 600000000)
        var hours = Math.floor(totalMinutes / 60)
        var minutes = totalMinutes % 60
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

    function focusPrimaryAction() {
        Qt.callLater(function() {
            thumbnailFocus.forceActiveFocus()
        })
    }

    onActiveFocusChanged: {
        if (activeFocus) {
            focusPrimaryAction()
        }
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            focusPrimaryAction()
        }
    }

    // ---- Background ----
    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundPrimary
    }

    // Blurred backdrop from series
    Image {
        id: backdropImage
        anchors.fill: parent
        source: thumbnailUrl
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        opacity: status === Image.Ready ? 0.35 : 0.0
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
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.5) }
            GradientStop { position: 0.4; color: Qt.rgba(0, 0, 0, 0.7) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.9) }
        }
    }

    // ---- Main layout ----
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingLarge * 1.5
        spacing: Theme.spacingLarge

        // Top: Countdown text
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(60 * Theme.layoutScale)

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: root.autoplay && root.countdown > 0
                text: "Next episode in " + root.countdown
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Font.DemiBold
                color: Theme.textSecondary
            }

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: root.autoplay && root.countdown <= 0 && root.countdown !== -1
                text: "Starting…"
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Font.DemiBold
                color: Theme.accentPrimary
            }

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.autoplay || root.countdown === -1
                text: "Up Next"
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Font.Bold
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
                    radius: Theme.radiusLarge
                    color: Theme.cardBackground
                    border.width: thumbnailFocus.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                    border.color: thumbnailFocus.activeFocus ? Theme.accentPrimary : Theme.cardBorder
                    clip: true

                    Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    Behavior on border.width { NumberAnimation { duration: Theme.durationShort } }

                    Image {
                        id: thumbImage
                        anchors.fill: parent
                        anchors.margins: Math.round(2 * Theme.layoutScale)
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
                        anchors.margins: Math.round(2 * Theme.layoutScale)
                        radius: Theme.radiusLarge - 1
                        visible: false
                        layer.enabled: true
                        layer.smooth: true
                    }

                    // Dark overlay + play icon
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Math.round(2 * Theme.layoutScale)
                        color: thumbnailFocus.activeFocus ? Qt.rgba(0, 0, 0, 0.25) : Qt.rgba(0, 0, 0, 0.4)
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }

                        // Play circle
                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.round(80 * Theme.layoutScale)
                            height: width
                            radius: width / 2
                            color: thumbnailFocus.activeFocus
                                ? Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.9)
                                : Qt.rgba(1, 1, 1, 0.15)
                            border.width: 2
                            border.color: Qt.rgba(1, 1, 1, 0.3)

                            Behavior on color { ColorAnimation { duration: Theme.durationShort } }

                            Text {
                                anchors.centerIn: parent
                                // Slight right offset for visual balance of play triangle
                                anchors.horizontalCenterOffset: Math.round(3 * Theme.layoutScale)
                                text: Icons.playArrow
                                font.family: Theme.fontIcon
                                font.pixelSize: Math.round(40 * Theme.layoutScale)
                                color: thumbnailFocus.activeFocus ? Theme.textOnAccent : Theme.textPrimary
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
                        height: Math.round(4 * Theme.layoutScale)
                        radius: 2
                        color: Qt.rgba(1, 1, 1, 0.2)

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            radius: 2
                            color: Theme.accentPrimary
                            width: {
                                var total = typeof ConfigManager !== 'undefined' ? ConfigManager.autoplayCountdownSeconds : 10
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
                    visible: root.seriesName !== ""
                    text: root.seriesName
                    font.pixelSize: Theme.fontSizeMedium
                    font.family: Theme.fontPrimary
                    font.weight: Font.DemiBold
                    color: Theme.accentPrimary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                // Episode identifier
                Text {
                    text: {
                        var prefix = "S" + root.seasonNumber + " E" + root.episodeNumber
                        if (root.episodeNumber === 0) prefix = "Special"
                        if (root.episodeName) prefix += " · " + root.episodeName
                        return prefix
                    }
                    font.pixelSize: Theme.fontSizeTitle
                    font.family: Theme.fontPrimary
                    font.bold: true
                    color: Theme.textPrimary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                // Metadata row: runtime, rating, year
                RowLayout {
                    spacing: Theme.spacingMedium

                    Text {
                        visible: root.runtimeTicks > 0
                        text: root.formatRuntime(root.runtimeTicks)
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                    }

                    Text {
                        visible: root.communityRating > 0
                        text: root.formatRating(root.communityRating)
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: "#FFD700"
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
                    visible: root.overview !== ""
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

                Item { Layout.fillHeight: true }
            }
        }

        // Bottom: Action buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeightLarge
            spacing: Theme.spacingLarge

            // "Back to Home" button
            Button {
                id: backToHomeBtn
                Layout.preferredHeight: Theme.buttonHeightLarge
                Layout.preferredWidth: Math.round(240 * Theme.layoutScale)
                focusPolicy: Qt.StrongFocus

                KeyNavigation.up: thumbnailFocus
                KeyNavigation.right: moreEpisodesBtn

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

                onClicked: {
                    root.cancelCountdown()
                    root.backToHomeRequested()
                }

                background: Rectangle {
                    radius: Theme.radiusMedium
                    color: {
                        if (backToHomeBtn.down) return Theme.buttonSecondaryBackgroundPressed
                        if (backToHomeBtn.hovered || backToHomeBtn.activeFocus) return Theme.buttonSecondaryBackgroundHover
                        return Theme.buttonSecondaryBackground
                    }
                    border.width: backToHomeBtn.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                    border.color: backToHomeBtn.activeFocus ? Theme.buttonSecondaryBorderFocused : Theme.buttonSecondaryBorder
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingSmall
                    Text {
                        text: Icons.home
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeIcon
                        color: Theme.textPrimary
                    }
                    Text {
                        text: qsTr("Back to Home")
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }
                }
            }

            // "More Episodes" button
            Button {
                id: moreEpisodesBtn
                Layout.preferredHeight: Theme.buttonHeightLarge
                Layout.preferredWidth: Math.round(260 * Theme.layoutScale)
                focusPolicy: Qt.StrongFocus

                KeyNavigation.up: thumbnailFocus
                KeyNavigation.left: backToHomeBtn

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

                onClicked: {
                    root.cancelCountdown()
                    root.moreEpisodesRequested()
                }

                background: Rectangle {
                    radius: Theme.radiusMedium
                    color: {
                        if (moreEpisodesBtn.down) return Theme.buttonSecondaryBackgroundPressed
                        if (moreEpisodesBtn.hovered || moreEpisodesBtn.activeFocus) return Theme.buttonSecondaryBackgroundHover
                        return Theme.buttonSecondaryBackground
                    }
                    border.width: moreEpisodesBtn.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                    border.color: moreEpisodesBtn.activeFocus ? Theme.buttonSecondaryBorderFocused : Theme.buttonSecondaryBorder
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingSmall
                    Text {
                        text: Icons.viewList
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeIcon
                        color: Theme.textPrimary
                    }
                    Text {
                        text: qsTr("More Episodes")
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }
    }

    // ESC cancels countdown and navigates to episode list
    Keys.onEscapePressed: function(event) {
        cancelCountdown()
        moreEpisodesRequested()
        event.accepted = true
    }

    // Initial focus
    Component.onCompleted: {
        focusPrimaryAction()
    }
}
