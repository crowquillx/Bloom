import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BloomUI

FocusScope {
    id: root
    focus: visible

    readonly property bool overlayActive: PlayerController.supportsEmbeddedVideo && PlayerController.isPlaybackActive
    readonly property bool paused: PlayerController.isPaused
    readonly property bool waitingForFirstFrame: overlayActive
                                                && !paused
                                                && PlayerController.currentPositionSeconds <= 0
                                                && PlayerController.durationSeconds <= 0
    readonly property bool buffering: overlayActive
                                     && (PlayerController.isLoading
                                         || PlayerController.isBuffering
                                         || waitingForFirstFrame)
    readonly property string mediaTitle: (PlayerController.overlayTitle && PlayerController.overlayTitle.length > 0)
                                        ? PlayerController.overlayTitle
                                        : qsTr("Now Playing")
    readonly property string mediaSubtitle: (PlayerController.overlaySubtitle && PlayerController.overlaySubtitle.length > 0)
                                           ? PlayerController.overlaySubtitle
                                           : (paused ? qsTr("Paused") : qsTr("Playing"))
    property bool controlsVisible: false
    property bool seekPreviewActive: false
    property bool seekOnlyMode: false
    property bool audioSelectorOpen: false
    property bool subtitleSelectorOpen: false
    readonly property bool selectorOpen: audioSelectorOpen || subtitleSelectorOpen
    readonly property bool fullControlsVisible: controlsVisible && !seekOnlyMode
    property bool hasPrimedPointerPosition: false
    property real lastPointerY: -1
    readonly property bool introSegmentActive: PlayerController.isInIntroSegment
    readonly property bool outroSegmentActive: PlayerController.isInOutroSegment
    readonly property string activeSkipSegmentType: introSegmentActive ? "intro" : (outroSegmentActive ? "outro" : "")
    readonly property string activeSkipLabel: introSegmentActive ? qsTr("Skip Intro") : qsTr("Skip Credits")
    readonly property int skipPopupDurationMs: Math.max(0, ConfigManager.skipButtonAutoHideSeconds * 1000)
    readonly property bool persistentSkipVisible: controlsVisible
                                                 && !skipPopupVisible
                                                 && !buffering
                                                 && activeSkipSegmentType.length > 0
    property bool skipPopupVisible: false
    property string skipPopupSegmentType: ""
    property double skipPopupWindowEndEpochMs: 0
    property int controlsAutoHideMs: 2500
    property int controlsHideAnimMs: 180
    property int seekPreviewHoldMs: 1800
    property int sideRailWidth: Math.round(300 * Theme.layoutScale)
    property real controlsSlideDistance: Math.round(28 * Theme.layoutScale)
    property real topControlsOffset: fullControlsVisible ? 0 : -controlsSlideDistance
    property real bottomControlsOffset: controlsVisible ? 0 : controlsSlideDistance
    default property alias overlayContent: overlayRoot.data

    anchors.fill: parent
    visible: overlayActive
    z: 200
    Behavior on topControlsOffset {
        NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
    }
    Behavior on bottomControlsOffset {
        NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
    }

    function formatTime(seconds) {
        var total = Math.max(0, Math.floor(seconds))
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var secs = total % 60
        if (hours > 0) {
            return hours + ":" + String(minutes).padStart(2, "0") + ":" + String(secs).padStart(2, "0")
        }
        return minutes + ":" + String(secs).padStart(2, "0")
    }

    function overlayHotzoneContains(yValue) {
        var topZone = Math.round(180 * Theme.layoutScale)
        var bottomZone = Math.round(250 * Theme.layoutScale)
        return yValue <= topZone || yValue >= (root.height - bottomZone)
    }

    function showSkipPopupIfEligible() {
        if (!overlayActive || controlsVisible || activeSkipSegmentType.length === 0 || skipPopupDurationMs <= 0) {
            skipPopupVisible = false
            skipPopupTimer.stop()
            return
        }
        var remainingMs = skipPopupWindowEndEpochMs > 0
                        ? Math.max(0, Math.floor(skipPopupWindowEndEpochMs - Date.now()))
                        : skipPopupDurationMs
        if (remainingMs <= 0) {
            skipPopupVisible = false
            skipPopupTimer.stop()
            return
        }
        skipPopupSegmentType = activeSkipSegmentType
        skipPopupVisible = true
        skipPopupTimer.interval = remainingMs
        skipPopupTimer.restart()
        root.forceActiveFocus(Qt.ActiveWindowFocusReason)
        Qt.callLater(function() { skipPopupButton.forceActiveFocus(Qt.ActiveWindowFocusReason) })
    }

    function triggerActiveSkip() {
        if (!overlayActive || activeSkipSegmentType.length === 0) {
            return
        }
        PlayerController.skipActiveSegment()
        skipPopupVisible = false
        skipPopupTimer.stop()
    }

    function showControls() {
        if (!overlayActive) {
            return
        }
        controlsVisible = true
        seekOnlyMode = false
        if (paused || seekPreviewActive || selectorOpen) {
            hideTimer.stop()
        } else {
            hideTimer.restart()
        }
    }

    function activateOverlayFocus() {
        if (!overlayActive) {
            return
        }
        showControls()
        root.forceActiveFocus(Qt.ShortcutFocusReason)
        Qt.callLater(function() { playPauseButton.forceActiveFocus(Qt.ShortcutFocusReason) })
    }

    function showSeekPreview(keepFullControls) {
        if (!overlayActive) {
            return
        }
        controlsVisible = true
        seekOnlyMode = keepFullControls === true ? false : true
        hideTimer.stop()
        seekPreviewActive = true
        seekPreviewTimer.restart()
    }

    function focusControl(target) {
        if (target) {
            target.forceActiveFocus()
        }
    }

    function buttonXInRoot(button, panelWidth) {
        var p = button.mapToItem(root, button.width / 2, 0)
        return Math.max(0, Math.min(root.width - panelWidth, p.x - panelWidth / 2))
    }

    function buttonYInRoot(button, panelHeight) {
        var p = button.mapToItem(root, 0, 0)
        return Math.max(0, p.y - panelHeight - Math.round(24 * Theme.layoutScale))
    }

    function audioListIndexForTrack(trackIndex) {
        var tracks = PlayerController.availableAudioTracks
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].index === trackIndex) {
                return i
            }
        }
        return tracks.length > 0 ? 0 : -1
    }

    readonly property var subtitleTrackOptions: {
        var options = [{ index: -1, displayTitle: qsTr("Off"), title: qsTr("No subtitles") }]
        var tracks = PlayerController.availableSubtitleTracks
        for (var i = 0; i < tracks.length; i++) {
            options.push(tracks[i])
        }
        return options
    }

    function subtitleListIndexForTrack(trackIndex) {
        var tracks = subtitleTrackOptions
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].index === trackIndex) {
                return i
            }
        }
        return tracks.length > 0 ? 0 : -1
    }

    function trackPrimaryLabel(track) {
        if (!track) return ""
        return track.displayTitle || track.title || qsTr("Track")
    }

    function trackSecondaryLabel(track, isSubtitle) {
        if (!track) return ""
        if (track.index === -1) return qsTr("Disabled")
        if (track.title && track.title.length > 0 && track.title !== track.displayTitle) return track.title
        if (isSubtitle) {
            if (track.isHearingImpaired) return qsTr("Subtitles for the Deaf and Hard of Hearing")
            if (track.isForced) return qsTr("Forced")
            return qsTr("Full")
        }
        if (track.channelLayout && track.channelLayout.length > 0) return track.channelLayout
        if (track.channels && track.channels > 0) return qsTr("%1.0").arg(track.channels)
        return ""
    }

    function openAudioSelector() {
        if (audioSelectorOpen) {
            audioSelectorOpen = false
            return
        }
        if (!PlayerController.availableAudioTracks || PlayerController.availableAudioTracks.length === 0) {
            return
        }
        subtitleSelectorOpen = false
        showControls()
        audioSelectorOpen = true
        var idx = audioListIndexForTrack(PlayerController.selectedAudioTrack)
        audioList.currentIndex = idx
        Qt.callLater(function() {
            if (idx >= 0) {
                audioList.positionViewAtIndex(idx, ListView.Contain)
            }
            audioList.forceActiveFocus()
        })
    }

    function openSubtitleSelector() {
        if (subtitleSelectorOpen) {
            subtitleSelectorOpen = false
            return
        }
        subtitleSelectorOpen = true
        audioSelectorOpen = false
        showControls()
        var idx = subtitleListIndexForTrack(PlayerController.selectedSubtitleTrack)
        subtitleList.currentIndex = idx
        Qt.callLater(function() {
            if (idx >= 0) {
                subtitleList.positionViewAtIndex(idx, ListView.Contain)
            }
            subtitleList.forceActiveFocus()
        })
    }

    function closeSelectors() {
        var wasOpen = selectorOpen
        audioSelectorOpen = false
        subtitleSelectorOpen = false
        if (wasOpen) {
            Qt.callLater(function() { playPauseButton.forceActiveFocus() })
        }
        return wasOpen
    }

    function handleDirectionalKey(direction) {
        if (!overlayActive) {
            return false
        }

        if (selectorOpen) {
            return false
        }

        if (seekOnlyMode) {
            if (direction === "left") {
                PlayerController.seekRelative(-10)
                showSeekPreview()
                return true
            }
            if (direction === "right") {
                PlayerController.seekRelative(10)
                showSeekPreview()
                return true
            }
            if (direction === "up" || direction === "down") {
                showControls()
                focusControl(playPauseButton)
                return true
            }
        }

        if (!fullControlsVisible) {
            showControls()
            focusControl(playPauseButton)
            return true
        }

        // Keep full overlay alive while navigating, but still allow forced auto-hide timer.
        showControls()

        var active = root.Window.activeFocusItem
        if (!active || !hasFocusedControl()) {
            focusControl(playPauseButton)
            return true
        }

        if (direction === "up") {
            if (active === backButton) {
                return true
            }
            if (active === progressFocus) {
                focusControl(backButton)
            } else {
                focusControl(progressFocus)
            }
            return true
        }

        if (direction === "down") {
            if (active === backButton || active === progressFocus) {
                focusControl(playPauseButton)
            }
            return true
        }

        if (direction === "left") {
            if (active === progressFocus) {
                PlayerController.seekRelative(-10)
                showSeekPreview(true)
                return true
            }
            if (active === persistentSkipButton) {
                focusControl(skipForwardButton)
                return true
            }
            if (active === audioButton) focusControl(volumeButton)
            else if (active === subtitleButton) focusControl(audioButton)
            else if (active === skipBackButton) focusControl(subtitleButton)
            else if (active === previousChapterButton) focusControl(skipBackButton)
            else if (active === playPauseButton) focusControl(previousChapterButton)
            else if (active === nextChapterButton) focusControl(playPauseButton)
            else if (active === skipForwardButton) focusControl(nextChapterButton)
            else if (active === volumeButton) focusControl(persistentSkipVisible ? persistentSkipButton : skipForwardButton)
            return true
        }

        if (direction === "right") {
            if (active === progressFocus) {
                PlayerController.seekRelative(10)
                showSeekPreview(true)
                return true
            }
            if (active === persistentSkipButton) {
                focusControl(volumeButton)
                return true
            }
            if (active === audioButton) focusControl(subtitleButton)
            else if (active === subtitleButton) focusControl(skipBackButton)
            else if (active === skipBackButton) focusControl(previousChapterButton)
            else if (active === previousChapterButton) focusControl(playPauseButton)
            else if (active === playPauseButton) focusControl(nextChapterButton)
            else if (active === nextChapterButton) focusControl(skipForwardButton)
            else if (active === skipForwardButton) focusControl(persistentSkipVisible ? persistentSkipButton : volumeButton)
            else if (active === volumeButton) focusControl(audioButton)
            return true
        }

        return false
    }

    function hasFocusedControl() {
        var active = root.Window.activeFocusItem
        while (active) {
            if (active === root) {
                return true
            }
            active = active.parent
        }
        return false
    }

    function hideControlsIfAllowed() {
        if (!overlayActive || paused || seekPreviewActive || selectorOpen) {
            return
        }
        controlsVisible = false
        seekOnlyMode = false
    }

    onOverlayActiveChanged: {
        if (overlayActive) {
            hideTimer.stop()
            seekPreviewTimer.stop()
            controlsVisible = false
            seekOnlyMode = false
            hasPrimedPointerPosition = false
            lastPointerY = -1
            showSkipPopupIfEligible()
        } else {
            hideTimer.stop()
            seekPreviewTimer.stop()
            skipPopupTimer.stop()
            audioSelectorOpen = false
            subtitleSelectorOpen = false
            controlsVisible = false
            seekPreviewActive = false
            seekOnlyMode = false
            skipPopupVisible = false
            skipPopupSegmentType = ""
            skipPopupWindowEndEpochMs = 0
        }
    }

    onPausedChanged: {
        if (!overlayActive) {
            return
        }
        // Avoid opening overlay during startup state churn; only refocus if controls are already shown.
        if (paused && controlsVisible) {
            activateOverlayFocus()
        } else if (!paused && controlsVisible && !seekPreviewActive && !selectorOpen) {
            hideTimer.restart()
        }
    }

    onControlsVisibleChanged: {
        if (controlsVisible) {
            skipPopupVisible = false
            skipPopupTimer.stop()
        } else {
            showSkipPopupIfEligible()
        }
        if (controlsVisible && !seekOnlyMode && overlayActive && !buffering && !selectorOpen) {
            if (root.Window.window) {
                root.Window.window.requestActivate()
            }
            root.forceActiveFocus(Qt.ActiveWindowFocusReason)
            Qt.callLater(function() { playPauseButton.forceActiveFocus(Qt.ActiveWindowFocusReason) })
        }
    }

    onSelectorOpenChanged: {
        if (selectorOpen) {
            hideTimer.stop()
        } else {
            showControls()
        }
    }

    onActiveSkipSegmentTypeChanged: {
        if (activeSkipSegmentType.length === 0) {
            skipPopupVisible = false
            skipPopupTimer.stop()
            skipPopupSegmentType = ""
            skipPopupWindowEndEpochMs = 0
            return
        }
        skipPopupWindowEndEpochMs = Date.now() + skipPopupDurationMs
        if (activeSkipSegmentType !== skipPopupSegmentType) {
            showSkipPopupIfEligible()
        }
    }

    Timer {
        id: hideTimer
        interval: root.controlsAutoHideMs
        repeat: false
        onTriggered: root.hideControlsIfAllowed()
    }

    Timer {
        id: seekPreviewTimer
        interval: root.seekPreviewHoldMs
        repeat: false
        onTriggered: {
            root.seekPreviewActive = false
            if (root.seekOnlyMode && !root.paused) {
                root.controlsVisible = false
                root.seekOnlyMode = false
            } else {
                root.showControls()
            }
        }
    }

    Timer {
        id: skipPopupTimer
        interval: root.skipPopupDurationMs
        repeat: false
        onTriggered: {
            root.skipPopupVisible = false
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        propagateComposedEvents: true
        cursorShape: InputModeManager.pointerActive ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: function(mouse) {
            if (!root.overlayActive) {
                return
            }
            if (!root.hasPrimedPointerPosition) {
                root.hasPrimedPointerPosition = true
                root.lastPointerY = mouse.y
                return
            }
            if (Math.abs(mouse.y - root.lastPointerY) < 0.5) {
                return
            }
            root.lastPointerY = mouse.y
            if (!root.controlsVisible && root.overlayHotzoneContains(mouse.y)) {
                root.showControls()
            }
        }
    }

    Keys.onPressed: function(event) {
        if (root.skipPopupVisible
                && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)) {
            event.accepted = true
            root.triggerActiveSkip()
            return
        }
        if (event.key === Qt.Key_Left) {
            event.accepted = root.handleDirectionalKey("left")
        } else if (event.key === Qt.Key_Right) {
            event.accepted = root.handleDirectionalKey("right")
        } else if (event.key === Qt.Key_Up) {
            event.accepted = root.handleDirectionalKey("up")
        } else if (event.key === Qt.Key_Down) {
            event.accepted = root.handleDirectionalKey("down")
        } else if (event.key === Qt.Key_I) {
            event.accepted = true
            if (event.modifiers & Qt.ShiftModifier) {
                PlayerController.showMpvStatsOnce()
            } else {
                PlayerController.toggleMpvStats()
            }
        } else if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            event.accepted = true
            PlayerController.showMpvStatsPage(event.key - Qt.Key_0)
        } else if (event.key === Qt.Key_Shift
                   || event.key === Qt.Key_Control
                   || event.key === Qt.Key_Alt
                   || event.key === Qt.Key_Meta) {
            event.accepted = false
        } else {
            event.accepted = false
        }
    }

    component GlassCircleButton: Button {
        id: glassButton
        property int diameter: Math.round(64 * Theme.layoutScale)
        property int iconSize: Math.round(32 * Theme.layoutScale)
        property bool hero: false
        property color iconColor: Theme.playbackIconColor
        width: diameter
        height: diameter
        activeFocusOnTab: true
        font.family: Theme.fontIcon
        font.pixelSize: iconSize
        scale: hovered ? 1.1 : 1.0
        onPressed: root.showControls()
        Keys.onReturnPressed: function(event) {
            event.accepted = true
            glassButton.animateClick()
        }
        Keys.onEnterPressed: function(event) {
            event.accepted = true
            glassButton.animateClick()
        }
        Keys.onSpacePressed: function(event) {
            event.accepted = true
            glassButton.animateClick()
        }
        background: Rectangle {
            radius: width / 2
            color: {
                if (glassButton.hero) {
                    return glassButton.hovered || glassButton.activeFocus
                           ? Theme.playbackControlGlassBackgroundHover
                           : Qt.rgba(1, 1, 1, 0.20)
                }
                return glassButton.hovered || glassButton.activeFocus
                       ? Theme.playbackControlGlassBackgroundHover
                       : Theme.playbackControlGlassBackground
            }
            border.width: glassButton.activeFocus ? Theme.buttonFocusBorderWidth : (glassButton.hero ? 2 : 1)
            border.color: glassButton.activeFocus
                          ? Theme.focusBorder
                          : (glassButton.hero ? Theme.playbackControlGlassBorderStrong : Theme.playbackControlGlassBorder)
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blurMax: 32
                blur: 0.35
            }
        }
        contentItem: Text {
            text: glassButton.text
            font: glassButton.font
            color: glassButton.iconColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Behavior on scale { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    }

    component SkipPillButton: Button {
        id: skipButton
        property bool compact: false
        property string labelText: ""
        property bool emphasized: false
        property bool showIcon: true
        focusPolicy: Qt.StrongFocus
        activeFocusOnTab: true
        implicitHeight: Math.round((compact ? 46 : 56) * Theme.layoutScale)
        implicitWidth: Math.round((compact ? 170 : 230) * Theme.layoutScale)
        leftPadding: Math.round((compact ? 16 : 20) * Theme.layoutScale)
        rightPadding: Math.round((compact ? 16 : 20) * Theme.layoutScale)
        onPressed: root.showControls()
        Keys.onReturnPressed: function(event) { event.accepted = true; skipButton.animateClick() }
        Keys.onEnterPressed: function(event) { event.accepted = true; skipButton.animateClick() }
        Keys.onSpacePressed: function(event) { event.accepted = true; skipButton.animateClick() }
        background: Rectangle {
            id: skipButtonBg
            radius: height / 2
            color: skipButton.hovered || skipButton.activeFocus
                   ? Theme.playbackControlGlassBackgroundHover
                   : (skipButton.emphasized
                      ? Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.18)
                      : Theme.playbackControlGlassBackground)
            border.width: skipButton.activeFocus ? Theme.buttonFocusBorderWidth : (skipButton.emphasized ? 2 : 1)
            border.color: skipButton.activeFocus
                          ? Theme.focusBorder
                          : (skipButton.emphasized ? Theme.accentPrimary : Theme.playbackControlGlassBorderStrong)
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blurMax: 30
                blur: 0.35
            }
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: skipButton.activeFocus ? Math.max(2, Theme.buttonFocusBorderWidth + 1) : 0
                border.color: Qt.rgba(Theme.focusBorder.r, Theme.focusBorder.g, Theme.focusBorder.b, 0.55)
                visible: skipButton.activeFocus
            }
            SequentialAnimation on opacity {
                running: skipButton.emphasized && skipButton.visible && !skipButton.activeFocus
                loops: Animation.Infinite
                NumberAnimation { to: 0.90; duration: 750; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 1.0; duration: 750; easing.type: Easing.InOutQuad }
            }
        }
        contentItem: Item {
            anchors.fill: parent
            implicitWidth: skipContentRow.implicitWidth
            implicitHeight: skipContentRow.implicitHeight
            Row {
                id: skipContentRow
                spacing: Math.round(8 * Theme.layoutScale)
                anchors.centerIn: parent
                Text {
                    text: Icons.fastForward
                    font.family: Theme.fontIcon
                    font.pixelSize: Math.round((compact ? 20 : 22) * Theme.layoutScale)
                    color: Theme.playbackIconColor
                    anchors.verticalCenter: parent.verticalCenter
                    visible: skipButton.showIcon
                }
                Text {
                    text: skipButton.labelText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Math.round((compact ? 18 : 21) * Theme.layoutScale)
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                    horizontalAlignment: Text.AlignHCenter
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.round(150 * Theme.layoutScale)
        visible: !root.buffering
        opacity: root.fullControlsVisible ? 1.0 : 0.0
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.playbackOverlayTopTint.r, Theme.playbackOverlayTopTint.g, Theme.playbackOverlayTopTint.b, 0.70) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        Behavior on opacity {
            NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.round(300 * Theme.layoutScale)
        visible: !root.buffering
        opacity: root.fullControlsVisible ? 1.0 : 0.0
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.60) }
        }
        Behavior on opacity {
            NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
        }
    }

    SkipPillButton {
        id: skipPopupButton
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Math.round(72 * Theme.layoutScale)
        anchors.bottomMargin: Math.round(182 * Theme.layoutScale)
        z: 640
        compact: false
        emphasized: true
        labelText: root.activeSkipLabel
        visible: opacity > 0.001
        opacity: root.skipPopupVisible && !root.buffering ? 1.0 : 0.0
        scale: root.skipPopupVisible && !root.buffering ? 1.0 : 0.95
        onClicked: root.triggerActiveSkip()
        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
    }

    Item {
        id: bufferingOverlay
        anchors.fill: parent
        visible: root.buffering
        z: 500

        Image {
            id: loadingBackdrop
            anchors.fill: parent
            source: PlayerController.overlayBackdropUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
            visible: source.toString().length > 0
            scale: 1.02

            SequentialAnimation on scale {
                running: bufferingOverlay.visible && loadingBackdrop.visible
                loops: Animation.Infinite
                NumberAnimation { to: 1.07; duration: 7000; easing.type: Easing.InOutCubic }
                NumberAnimation { to: 1.02; duration: 7000; easing.type: Easing.InOutCubic }
            }
        }

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(Theme.backgroundPrimary.r, Theme.backgroundPrimary.g, Theme.backgroundPrimary.b, loadingBackdrop.visible ? 0.58 : 0.78)
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.22) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.68) }
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.30) }
                GradientStop { position: 0.24; color: "transparent" }
                GradientStop { position: 0.76; color: "transparent" }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.44) }
            }
        }

        Rectangle {
            id: loadingCard
            anchors.centerIn: parent
            width: Math.round(Math.min(parent.width * 0.56, 640 * Theme.layoutScale))
            height: Math.round(300 * Theme.layoutScale)
            radius: Theme.radiusXLarge
            color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.82)
            border.width: 1
            border.color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.38)
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blurMax: 44
                blur: 0.34
                shadowEnabled: true
                shadowColor: Qt.rgba(0, 0, 0, 0.56)
                shadowBlur: 0.95
                shadowVerticalOffset: 12
            }
        }

        Column {
            anchors.centerIn: parent
            width: loadingCard.width - Math.round(72 * Theme.layoutScale)
            spacing: Math.round(16 * Theme.layoutScale)

            Item {
                id: spinner
                width: Math.round(80 * Theme.layoutScale)
                height: width
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: Math.round(70 * Theme.layoutScale)
                    height: width
                    radius: width / 2
                    color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.18)
                    border.width: 1
                    border.color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.46)
                    opacity: 0.8

                    SequentialAnimation on opacity {
                        running: bufferingOverlay.visible
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.38; duration: 760; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 0.86; duration: 760; easing.type: Easing.InOutQuad }
                    }
                }

                Repeater {
                    model: 12
                    Item {
                        width: spinner.width
                        height: spinner.height
                        rotation: index * 30

                        Rectangle {
                            width: Math.round(6 * Theme.layoutScale)
                            height: Math.round(16 * Theme.layoutScale)
                            radius: width / 2
                            color: Theme.accentPrimary
                            opacity: (index + 1) / 12
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: Math.round(6 * Theme.layoutScale)
                        }
                    }
                }

                RotationAnimator on rotation {
                    from: 0
                    to: 360
                    duration: 900
                    loops: Animation.Infinite
                    running: bufferingOverlay.visible
                }
            }

            Text {
                text: qsTr("Buffering...")
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: Math.round(32 * Theme.layoutScale)
                font.weight: Font.DemiBold
            }

            Text {
                text: qsTr("Connecting to server")
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.textSecondary
                font.family: Theme.fontPrimary
                font.pixelSize: Math.round(20 * Theme.layoutScale)
            }

            Text {
                text: root.mediaTitle
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                color: Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g, Theme.textPrimary.b, 0.82)
                font.family: Theme.fontPrimary
                font.pixelSize: Math.round(18 * Theme.layoutScale)
                elide: Text.ElideRight
            }
        }
    }

    Item {
        anchors.fill: parent
        visible: !root.buffering
        enabled: root.controlsVisible

        Row {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: Math.round(48 * Theme.layoutScale) + root.topControlsOffset
            anchors.leftMargin: Math.round(64 * Theme.layoutScale)
            spacing: Math.round(32 * Theme.layoutScale)
            opacity: root.fullControlsVisible ? 1.0 : 0.0
            enabled: root.fullControlsVisible
            Behavior on opacity {
                NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
            }

            GlassCircleButton {
                id: backButton
                diameter: Math.round(56 * Theme.layoutScale)
                iconSize: Math.round(28 * Theme.layoutScale)
                text: Icons.arrowBack
                onClicked: PlayerController.stop()
                KeyNavigation.down: progressFocus
                KeyNavigation.right: progressFocus
            }

            Column {
                spacing: Math.round(4 * Theme.layoutScale)
                anchors.verticalCenter: backButton.verticalCenter
                width: parent.width - backButton.width - parent.spacing

                Text {
                    text: root.mediaTitle
                    color: Theme.textPrimary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Math.round(36 * Theme.layoutScale)
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    width: parent.width
                }

                Text {
                    text: root.mediaSubtitle
                    color: Qt.rgba(1, 1, 1, 0.8)
                    font.family: Theme.fontPrimary
                    font.pixelSize: Math.round(24 * Theme.layoutScale)
                    elide: Text.ElideRight
                    width: parent.width
                }
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Math.round(64 * Theme.layoutScale)
            anchors.rightMargin: Math.round(64 * Theme.layoutScale)
            anchors.bottomMargin: Math.round(64 * Theme.layoutScale) - root.bottomControlsOffset
            spacing: Math.round(32 * Theme.layoutScale)
            opacity: root.controlsVisible ? 1.0 : 0.0
            enabled: root.controlsVisible
            Behavior on opacity {
                NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
            }

            Column {
                spacing: Math.round(12 * Theme.layoutScale)
                width: parent.width

                Item {
                    id: trickplayBubble
                    width: Math.round(240 * Theme.layoutScale)
                    height: Math.round(136 * Theme.layoutScale)
                    readonly property real anchorX: (progressMouse.containsMouse && progressTrack.width > 0)
                                                    ? Math.max(0, Math.min(progressTrack.width, progressMouse.mouseX))
                                                    : progressFill.width
                    x: Math.max(0, Math.min(progressTrack.width - width, anchorX - width / 2))
                    visible: PlayerController.hasTrickplay && (seekPreviewActive || progressMouse.containsMouse)

                    Rectangle {
                        id: trickplayFrame
                        anchors.fill: parent
                        radius: Theme.radiusLarge
                        color: Theme.cardBackground
                        border.width: 1
                        border.color: Theme.cardBorder
                        clip: true

                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            source: PlayerController.trickplayPreviewUrl
                            fillMode: Image.PreserveAspectCrop
                            cache: false
                            asynchronous: true
                            smooth: true
                        }
                    }
                }

                Rectangle {
                    id: progressTrack
                    width: parent.width
                    height: Math.round(12 * Theme.layoutScale)
                    radius: height / 2
                    color: Theme.playbackProgressTrack
                    border.width: 1
                    border.color: Theme.playbackControlGlassBorder
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        blurEnabled: true
                        blurMax: 24
                        blur: 0.30
                    }

                    Rectangle {
                        id: progressFill
                        width: parent.width * PlayerController.progressRatio
                        height: parent.height
                        radius: parent.radius
                        color: Theme.playbackProgressFill
                    }

                    Rectangle {
                        width: Math.round(20 * Theme.layoutScale)
                        height: width
                        radius: width / 2
                        color: Theme.playbackProgressFill
                        visible: progressMouse.containsMouse || progressFocus.activeFocus || root.seekPreviewActive
                        x: Math.max(0, progressFill.width - width / 2)
                        y: (parent.height - height) / 2
                    }

                    MouseArea {
                        id: progressMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onPositionChanged: function(mouse) {
                            if (!PlayerController.hasTrickplay || progressTrack.width <= 0 || PlayerController.durationSeconds <= 0) {
                                return
                            }
                            var ratio = Math.max(0, Math.min(1, mouse.x / progressTrack.width))
                            PlayerController.setTrickplayPreviewPositionSeconds(PlayerController.durationSeconds * ratio)
                        }
                        onPressed: function(mouse) {
                            if (progressTrack.width <= 0 || PlayerController.durationSeconds <= 0) {
                                return
                            }
                            var ratio = Math.max(0, Math.min(1, mouse.x / progressTrack.width))
                            PlayerController.seek(PlayerController.durationSeconds * ratio)
                            root.showSeekPreview(true)
                        }
                        onEntered: function() {
                            root.seekPreviewActive = true
                            if (!PlayerController.hasTrickplay || progressTrack.width <= 0 || PlayerController.durationSeconds <= 0) {
                                return
                            }
                            var ratio = Math.max(0, Math.min(1, mouseX / progressTrack.width))
                            PlayerController.setTrickplayPreviewPositionSeconds(PlayerController.durationSeconds * ratio)
                        }
                        onExited: function() {
                            root.seekPreviewActive = false
                            PlayerController.clearTrickplayPreviewPositionOverride()
                        }
                    }
                }

                FocusScope {
                    id: progressFocus
                    width: parent.width
                    height: Math.round(34 * Theme.layoutScale)
                    activeFocusOnTab: true
                    KeyNavigation.up: backButton
                    KeyNavigation.down: playPauseButton
                    Keys.onLeftPressed: function(event) {
                        event.accepted = true
                        PlayerController.seekRelative(-10)
                        root.showSeekPreview(true)
                    }
                    Keys.onRightPressed: function(event) {
                        event.accepted = true
                        PlayerController.seekRelative(10)
                        root.showSeekPreview(true)
                    }

                    RowLayout {
                        anchors.fill: parent
                        Text {
                            text: root.formatTime(PlayerController.currentPositionSeconds)
                            color: Theme.playbackTimePrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Math.round(20 * Theme.layoutScale)
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.formatTime(PlayerController.durationSeconds)
                            color: Theme.playbackTimeSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Math.round(20 * Theme.layoutScale)
                        }
                    }
                }
            }

            Item {
                visible: root.fullControlsVisible
                width: parent.width
                height: Math.round(116 * Theme.layoutScale)

                Item {
                    id: leftRail
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.sideRailWidth
                    height: parent.height

                    RowLayout {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Math.round(24 * Theme.layoutScale)

                        GlassCircleButton {
                            id: audioButton
                            diameter: Math.round(64 * Theme.layoutScale)
                            iconSize: Math.round(32 * Theme.layoutScale)
                            text: Icons.audiotrack
                            onClicked: root.openAudioSelector()
                            KeyNavigation.right: subtitleButton
                            KeyNavigation.left: volumeButton
                            KeyNavigation.up: progressFocus
                        }

                        GlassCircleButton {
                            id: subtitleButton
                            diameter: Math.round(64 * Theme.layoutScale)
                            iconSize: Math.round(32 * Theme.layoutScale)
                            text: Icons.subtitles
                            onClicked: root.openSubtitleSelector()
                            KeyNavigation.left: audioButton
                            KeyNavigation.right: skipBackButton
                            KeyNavigation.up: progressFocus
                        }
                    }
                }

                RowLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Math.round(24 * Theme.layoutScale)

                    GlassCircleButton {
                        id: skipBackButton
                        diameter: Math.round(64 * Theme.layoutScale)
                        iconSize: Math.round(28 * Theme.layoutScale)
                        text: Icons.fastRewind
                        onClicked: {
                            PlayerController.seekRelative(-10)
                            root.showSeekPreview(true)
                        }
                        KeyNavigation.left: subtitleButton
                        KeyNavigation.right: previousChapterButton
                        KeyNavigation.up: progressFocus
                    }

                    GlassCircleButton {
                        id: previousChapterButton
                        diameter: Math.round(80 * Theme.layoutScale)
                        iconSize: Math.round(32 * Theme.layoutScale)
                        text: Icons.skipPrevious
                        onClicked: PlayerController.previousChapter()
                        KeyNavigation.left: skipBackButton
                        KeyNavigation.right: playPauseButton
                        KeyNavigation.up: progressFocus
                    }

                    GlassCircleButton {
                        id: playPauseButton
                        diameter: Math.round(112 * Theme.layoutScale)
                        iconSize: Math.round(56 * Theme.layoutScale)
                        hero: true
                        text: root.paused ? Icons.playArrow : Icons.pause
                        onClicked: PlayerController.togglePause()
                        KeyNavigation.left: previousChapterButton
                        KeyNavigation.right: nextChapterButton
                        KeyNavigation.up: progressFocus
                    }

                    GlassCircleButton {
                        id: nextChapterButton
                        diameter: Math.round(80 * Theme.layoutScale)
                        iconSize: Math.round(32 * Theme.layoutScale)
                        text: Icons.skipNext
                        onClicked: PlayerController.nextChapter()
                        KeyNavigation.left: playPauseButton
                        KeyNavigation.right: skipForwardButton
                        KeyNavigation.up: progressFocus
                    }

                    GlassCircleButton {
                        id: skipForwardButton
                        diameter: Math.round(64 * Theme.layoutScale)
                        iconSize: Math.round(28 * Theme.layoutScale)
                        text: Icons.fastForward
                        onClicked: {
                            PlayerController.seekRelative(10)
                            root.showSeekPreview(true)
                        }
                        KeyNavigation.left: nextChapterButton
                        KeyNavigation.right: persistentSkipVisible ? persistentSkipButton : volumeButton
                        KeyNavigation.up: progressFocus
                    }
                }

                Item {
                    id: rightRail
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.sideRailWidth
                    height: parent.height

                    RowLayout {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Math.round(22 * Theme.layoutScale)

                        Item {
                            Layout.preferredWidth: Math.round(170 * Theme.layoutScale)
                            Layout.alignment: Qt.AlignVCenter
                            implicitHeight: persistentSkipButton.implicitHeight

                            SkipPillButton {
                                id: persistentSkipButton
                                anchors.centerIn: parent
                                compact: true
                                emphasized: false
                                showIcon: false
                                labelText: root.activeSkipLabel
                                enabled: root.persistentSkipVisible
                                opacity: root.persistentSkipVisible ? 1.0 : 0.0
                                onClicked: root.triggerActiveSkip()
                                KeyNavigation.left: skipForwardButton
                                KeyNavigation.right: volumeButton
                                KeyNavigation.up: progressFocus
                                KeyNavigation.down: progressFocus
                                Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                            }
                        }

                        GlassCircleButton {
                            id: volumeButton
                            diameter: Math.round(64 * Theme.layoutScale)
                            iconSize: Math.round(32 * Theme.layoutScale)
                            text: Icons.volumeUp
                            onClicked: PlayerController.toggleMute()
                            KeyNavigation.left: persistentSkipVisible ? persistentSkipButton : skipForwardButton
                            KeyNavigation.right: audioButton
                            KeyNavigation.up: progressFocus
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: audioPanel
        visible: root.audioSelectorOpen
        z: 1200
        width: Math.round(460 * Theme.layoutScale)
        height: Math.min(Math.round(560 * Theme.layoutScale), Math.round(root.height * 0.72))
        x: root.buttonXInRoot(audioButton, width)
        y: root.buttonYInRoot(audioButton, height)
        radius: Theme.radiusXLarge
        color: Qt.rgba(0.01, 0.02, 0.05, 0.92)
        border.width: 1
        border.color: Theme.cardBorder
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Math.round(28 * Theme.layoutScale)
            spacing: Math.round(16 * Theme.layoutScale)
            Text {
                text: qsTr("Audio Track")
                color: Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: Math.round(42 * Theme.layoutScale * 0.6)
                font.weight: Font.DemiBold
            }
            ListView {
                id: audioList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: PlayerController.availableAudioTracks
                focus: root.audioSelectorOpen
                spacing: Math.round(10 * Theme.layoutScale)
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    width: audioList.width
                    height: Math.round(72 * Theme.layoutScale)
                    color: audioList.currentIndex === index ? Theme.hoverOverlay : "transparent"
                    radius: Theme.radiusMedium
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            PlayerController.setSelectedAudioTrack(modelData.index)
                            root.audioSelectorOpen = false
                            Qt.callLater(function() { audioButton.forceActiveFocus() })
                        }
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(10 * Theme.layoutScale)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Math.round(3 * Theme.layoutScale)
                        width: parent.width - Math.round(64 * Theme.layoutScale)
                        Text {
                            text: root.trackPrimaryLabel(modelData)
                            color: Theme.textPrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Math.round(36 * Theme.layoutScale * 0.58)
                            elide: Text.ElideRight
                            width: parent.width
                        }
                        Text {
                            text: root.trackSecondaryLabel(modelData, false)
                            color: Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Math.round(28 * Theme.layoutScale * 0.58)
                            visible: text.length > 0
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: Math.round(8 * Theme.layoutScale)
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.index === PlayerController.selectedAudioTrack ? Icons.check : ""
                        font.family: Theme.fontIcon
                        font.pixelSize: Math.round(36 * Theme.layoutScale * 0.58)
                        color: Theme.textPrimary
                    }
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Up) {
                        event.accepted = true
                        currentIndex = Math.max(0, currentIndex - 1)
                        positionViewAtIndex(currentIndex, ListView.Contain)
                    } else if (event.key === Qt.Key_Down) {
                        event.accepted = true
                        currentIndex = Math.min(count - 1, currentIndex + 1)
                        positionViewAtIndex(currentIndex, ListView.Contain)
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        event.accepted = true
                        if (currentIndex >= 0 && currentIndex < count) {
                            var audioTracks = PlayerController.availableAudioTracks
                            PlayerController.setSelectedAudioTrack(audioTracks[currentIndex].index)
                            root.audioSelectorOpen = false
                            Qt.callLater(function() { audioButton.forceActiveFocus() })
                        }
                    } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
                        event.accepted = true
                        root.audioSelectorOpen = false
                        Qt.callLater(function() { audioButton.forceActiveFocus() })
                    } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                        event.accepted = true
                    }
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
        }
    }

    Rectangle {
        id: subtitlePanel
        visible: root.subtitleSelectorOpen
        z: 1200
        width: Math.round(520 * Theme.layoutScale)
        height: Math.min(Math.round(560 * Theme.layoutScale), Math.round(root.height * 0.72))
        x: root.buttonXInRoot(subtitleButton, width)
        y: root.buttonYInRoot(subtitleButton, height)
        radius: Theme.radiusXLarge
        color: Qt.rgba(0.01, 0.02, 0.05, 0.92)
        border.width: 1
        border.color: Theme.cardBorder
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Math.round(28 * Theme.layoutScale)
            spacing: Math.round(16 * Theme.layoutScale)
            Text {
                text: qsTr("Subtitles")
                color: Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: Math.round(42 * Theme.layoutScale * 0.6)
                font.weight: Font.DemiBold
            }
            ListView {
                id: subtitleList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.subtitleTrackOptions
                focus: root.subtitleSelectorOpen
                spacing: Math.round(10 * Theme.layoutScale)
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    width: subtitleList.width
                    height: Math.round(72 * Theme.layoutScale)
                    color: subtitleList.currentIndex === index ? Theme.hoverOverlay : "transparent"
                    radius: Theme.radiusMedium
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            PlayerController.setSelectedSubtitleTrack(modelData.index)
                            root.subtitleSelectorOpen = false
                            Qt.callLater(function() { subtitleButton.forceActiveFocus() })
                        }
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(10 * Theme.layoutScale)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Math.round(3 * Theme.layoutScale)
                        width: parent.width - Math.round(64 * Theme.layoutScale)
                        Text {
                            text: root.trackPrimaryLabel(modelData)
                            color: Theme.textPrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Math.round(36 * Theme.layoutScale * 0.58)
                            elide: Text.ElideRight
                            width: parent.width
                        }
                        Text {
                            text: root.trackSecondaryLabel(modelData, true)
                            color: Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Math.round(28 * Theme.layoutScale * 0.58)
                            visible: text.length > 0
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: Math.round(8 * Theme.layoutScale)
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.index === PlayerController.selectedSubtitleTrack ? Icons.check : ""
                        font.family: Theme.fontIcon
                        font.pixelSize: Math.round(36 * Theme.layoutScale * 0.58)
                        color: Theme.textPrimary
                    }
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Up) {
                        event.accepted = true
                        currentIndex = Math.max(0, currentIndex - 1)
                        positionViewAtIndex(currentIndex, ListView.Contain)
                    } else if (event.key === Qt.Key_Down) {
                        event.accepted = true
                        currentIndex = Math.min(count - 1, currentIndex + 1)
                        positionViewAtIndex(currentIndex, ListView.Contain)
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        event.accepted = true
                        if (currentIndex >= 0 && currentIndex < count) {
                            PlayerController.setSelectedSubtitleTrack(root.subtitleTrackOptions[currentIndex].index)
                            root.subtitleSelectorOpen = false
                            Qt.callLater(function() { subtitleButton.forceActiveFocus() })
                        }
                    } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
                        event.accepted = true
                        root.subtitleSelectorOpen = false
                        Qt.callLater(function() { subtitleButton.forceActiveFocus() })
                    } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                        event.accepted = true
                    }
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
        }
    }

    Item {
        id: overlayRoot
        visible: !root.buffering
        enabled: root.controlsVisible
        anchors.fill: parent
        opacity: root.controlsVisible ? 1.0 : 0.0
        Behavior on opacity {
            NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
        }
    }
}
