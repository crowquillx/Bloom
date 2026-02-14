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
    readonly property bool paused: PlayerController.playbackState === PlayerController.Paused
    readonly property string mediaTitle: (PlayerController.overlayTitle && PlayerController.overlayTitle.length > 0)
                                        ? PlayerController.overlayTitle
                                        : ((PlayerController.currentItemId && PlayerController.currentItemId.length > 0)
                                           ? PlayerController.currentItemId
                                           : qsTr("Now Playing"))
    readonly property string mediaSubtitle: (PlayerController.overlaySubtitle && PlayerController.overlaySubtitle.length > 0)
                                           ? PlayerController.overlaySubtitle
                                           : (paused ? qsTr("Paused") : qsTr("Playing"))
    property bool controlsVisible: false
    property bool seekPreviewActive: false
    property int controlsAutoHideMs: 2500
    default property alias overlayContent: overlayRoot.data

    anchors.fill: parent
    visible: overlayActive
    z: 200

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

    function showControls() {
        if (!overlayActive) {
            return
        }
        controlsVisible = true
        if (paused || seekPreviewActive || hasFocusedControl()) {
            hideTimer.stop()
        } else {
            hideTimer.restart()
        }
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
        if (!overlayActive || paused || seekPreviewActive || hasFocusedControl()) {
            return
        }
        controlsVisible = false
    }

    onOverlayActiveChanged: {
        if (overlayActive) {
            showControls()
            Qt.callLater(function() { playPauseButton.forceActiveFocus() })
        } else {
            hideTimer.stop()
            controlsVisible = false
            seekPreviewActive = false
        }
    }

    onPausedChanged: {
        if (!overlayActive) {
            return
        }
        if (paused) {
            controlsVisible = true
            hideTimer.stop()
        } else {
            showControls()
        }
    }

    Timer {
        id: hideTimer
        interval: root.controlsAutoHideMs
        repeat: false
        onTriggered: root.hideControlsIfAllowed()
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        propagateComposedEvents: true
        cursorShape: InputModeManager.pointerActive ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: root.showControls()
    }

    Keys.onPressed: function(event) {
        root.showControls()
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

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.round(150 * Theme.layoutScale)
        visible: root.controlsVisible
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.playbackOverlayTopTint.r, Theme.playbackOverlayTopTint.g, Theme.playbackOverlayTopTint.b, 0.70) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.round(300 * Theme.layoutScale)
        visible: root.controlsVisible
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.60) }
        }
    }

    Item {
        anchors.fill: parent
        visible: root.controlsVisible

        Row {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: Math.round(48 * Theme.layoutScale)
            anchors.leftMargin: Math.round(64 * Theme.layoutScale)
            spacing: Math.round(32 * Theme.layoutScale)

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
            anchors.bottomMargin: Math.round(64 * Theme.layoutScale)
            spacing: Math.round(32 * Theme.layoutScale)

            Column {
                spacing: Math.round(12 * Theme.layoutScale)
                width: parent.width

                Item {
                    id: trickplayBubble
                    width: Math.round(240 * Theme.layoutScale)
                    height: Math.round(136 * Theme.layoutScale)
                    x: Math.max(0, Math.min(progressTrack.width - width, progressFill.width - width / 2))
                    visible: seekPreviewActive || progressMouse.containsMouse

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusLarge
                        color: Theme.cardBackground
                        border.width: 1
                        border.color: Theme.cardBorder
                    }

                    Text {
                        anchors.centerIn: parent
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeSmall
                        text: PlayerController.hasTrickplay ? qsTr("Trickplay preview") : qsTr("Preview coming soon")
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
                        onPressed: function(mouse) {
                            if (progressTrack.width <= 0 || PlayerController.durationSeconds <= 0) {
                                return
                            }
                            var ratio = Math.max(0, Math.min(1, mouse.x / progressTrack.width))
                            PlayerController.seek(PlayerController.durationSeconds * ratio)
                            root.showControls()
                        }
                        onEntered: root.seekPreviewActive = true
                        onExited: root.seekPreviewActive = false
                    }
                }

                FocusScope {
                    id: progressFocus
                    width: parent.width
                    height: Math.round(34 * Theme.layoutScale)
                    activeFocusOnTab: true
                    KeyNavigation.up: backButton
                    KeyNavigation.down: playPauseButton
                    Keys.onLeftPressed: {
                        PlayerController.seekRelative(-10)
                        root.seekPreviewActive = true
                        root.showControls()
                    }
                    Keys.onRightPressed: {
                        PlayerController.seekRelative(10)
                        root.seekPreviewActive = true
                        root.showControls()
                    }
                    Keys.onReleased: function(event) {
                        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                            root.seekPreviewActive = false
                        }
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

            RowLayout {
                width: parent.width

                RowLayout {
                    spacing: Math.round(24 * Theme.layoutScale)

                    GlassCircleButton {
                        id: audioButton
                        diameter: Math.round(64 * Theme.layoutScale)
                        iconSize: Math.round(32 * Theme.layoutScale)
                        text: Icons.audiotrack
                        onClicked: PlayerController.cycleAudioTrack()
                        KeyNavigation.right: subtitleButton
                        KeyNavigation.left: volumeButton
                        KeyNavigation.up: progressFocus
                    }

                    GlassCircleButton {
                        id: subtitleButton
                        diameter: Math.round(64 * Theme.layoutScale)
                        iconSize: Math.round(32 * Theme.layoutScale)
                        text: Icons.subtitles
                        onClicked: PlayerController.cycleSubtitleTrack()
                        KeyNavigation.left: audioButton
                        KeyNavigation.right: skipBackButton
                        KeyNavigation.up: progressFocus
                    }
                }

                Item { Layout.fillWidth: true }

                RowLayout {
                    spacing: Math.round(24 * Theme.layoutScale)

                    GlassCircleButton {
                        id: skipBackButton
                        diameter: Math.round(64 * Theme.layoutScale)
                        iconSize: Math.round(28 * Theme.layoutScale)
                        text: Icons.fastRewind
                        onClicked: PlayerController.seekRelative(-10)
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
                        onClicked: PlayerController.seekRelative(10)
                        KeyNavigation.left: nextChapterButton
                        KeyNavigation.right: volumeButton
                        KeyNavigation.up: progressFocus
                    }
                }

                Item { Layout.fillWidth: true }

                RowLayout {
                    GlassCircleButton {
                        id: volumeButton
                        diameter: Math.round(64 * Theme.layoutScale)
                        iconSize: Math.round(32 * Theme.layoutScale)
                        text: Icons.volumeUp
                        onClicked: PlayerController.toggleMute()
                        KeyNavigation.left: skipForwardButton
                        KeyNavigation.right: audioButton
                        KeyNavigation.up: progressFocus
                    }
                }
            }
        }
    }

    Item {
        id: overlayRoot
        visible: root.controlsVisible
        anchors.fill: parent
    }

    Component.onCompleted: showControls()
}
