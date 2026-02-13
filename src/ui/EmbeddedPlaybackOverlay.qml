import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

Item {
    id: root

    readonly property bool overlayActive: PlayerController.supportsEmbeddedVideo && PlayerController.isPlaybackActive
    readonly property bool paused: PlayerController.playbackState === PlayerController.Paused
    readonly property bool backendAgnostic: true
    default property alias overlayContent: overlayRoot.data

    anchors.fill: parent
    visible: overlayActive
    z: 200

    Rectangle {
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
        anchors.fill: parent
    }
}
