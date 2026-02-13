import QtQuick
import BloomUI

Item {
    id: root

    property bool shrinkEnabled: PlayerController.embeddedVideoShrinkEnabled

    MpvVideoItem {
        id: videoTarget
        anchors {
            right: parent.right
            bottom: parent.bottom
            rightMargin: shrinkEnabled ? Theme.spacingLg : 0
            bottomMargin: shrinkEnabled ? Theme.spacingLg : 0
        }
        width: shrinkEnabled ? parent.width * 0.72 : parent.width
        height: shrinkEnabled ? (parent.height * 0.72) : parent.height

        onViewportChanged: function(x, y, width, height) {
            PlayerController.setEmbeddedVideoViewport(x, y, width, height)
        }

        function syncEmbeddedViewport() {
            if (width > 0 && height > 0) {
                PlayerController.setEmbeddedVideoViewport(0, 0, width, height)
            }
        }

        onWidthChanged: syncEmbeddedViewport()
        onHeightChanged: syncEmbeddedViewport()

        Component.onCompleted: {
            PlayerController.attachEmbeddedVideoTarget(videoTarget)
            if (width > 0 && height > 0) {
                PlayerController.setEmbeddedVideoViewport(0, 0, width, height)
            } else {
                Qt.callLater(syncEmbeddedViewport)
            }
        }

        Component.onDestruction: {
            PlayerController.detachEmbeddedVideoTarget(videoTarget)
        }
    }
}
