import QtQuick
import BloomUI

Item {
    id: root

    readonly property bool overlayActive: PlayerController.supportsEmbeddedVideo && PlayerController.isPlaybackActive
    readonly property bool paused: PlayerController.playbackState === PlayerController.Paused
    readonly property bool backendAgnostic: true
    default property alias overlayContent: overlayRoot.data

    anchors.fill: parent
    visible: overlayActive
    z: 0

    Item {
        id: overlayRoot
        anchors.fill: parent
    }
}
