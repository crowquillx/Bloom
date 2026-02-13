import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root
    focus: visible

    readonly property bool overlayActive: PlayerController.supportsEmbeddedVideo && PlayerController.isPlaybackActive
    readonly property bool paused: PlayerController.playbackState === PlayerController.Paused
    property bool controlsVisible: false
    property int controlsAutoHideMs: 2500
    readonly property bool backendAgnostic: true
    default property alias overlayContent: overlayRoot.data

    anchors.fill: parent
    visible: overlayActive
    z: 200

    function showControls() {
        if (!overlayActive) {
            return
        }

        controlsVisible = true
        if (paused) {
            hideTimer.stop()
        } else {
            hideTimer.restart()
        }
    }

    function hideControlsIfAllowed() {
        if (!overlayActive || paused) {
            return
        }

        controlsVisible = false
    }

    onOverlayActiveChanged: {
        if (overlayActive) {
            showControls()
        } else {
            hideTimer.stop()
            controlsVisible = false
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

    Rectangle {
        visible: root.controlsVisible
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.max(72, Theme.spacingXLarge + Theme.spacingMedium)
        color: Theme.overlayTextBackground
        opacity: 0.88

        RowLayout {
            anchors.centerIn: parent
            spacing: Theme.spacingMedium

            Button {
                text: Icons.fastRewind
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                onClicked: PlayerController.seekRelative(-10)
            }

            Button {
                text: paused ? Icons.playArrow : Icons.pause
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                onClicked: PlayerController.togglePause()
            }

            Button {
                text: Icons.stop
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                onClicked: PlayerController.stop()
            }

            Button {
                text: Icons.fastForward
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                onClicked: PlayerController.seekRelative(10)
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
