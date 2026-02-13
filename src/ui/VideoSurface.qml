import QtQuick
import BloomUI

Item {
    id: root

    property bool shrinkEnabled: PlayerController.embeddedVideoShrinkEnabled
    readonly property int overlayReservedHeight: PlayerController.isPlaybackActive
                                              ? Math.max(72, Theme.spacingXLarge + Theme.spacingMedium)
                                              : 0

    MpvVideoItem {
        id: videoTarget
        anchors {
            right: parent.right
            bottom: parent.bottom
            rightMargin: shrinkEnabled ? Theme.spacingLg : 0
            bottomMargin: (shrinkEnabled ? Theme.spacingLg : 0) + root.overlayReservedHeight
        }
        width: shrinkEnabled ? parent.width * 0.72 : parent.width
        height: shrinkEnabled
              ? (parent.height * 0.72)
              : Math.max(1, parent.height - root.overlayReservedHeight)

        onViewportChanged: function(x, y, width, height) {
            PlayerController.setEmbeddedVideoViewport(x, y, width, height)
        }

        Component.onCompleted: {
            PlayerController.attachEmbeddedVideoTarget(videoTarget)
            PlayerController.setEmbeddedVideoViewport(0, 0, width, height)
        }

        Component.onDestruction: {
            PlayerController.detachEmbeddedVideoTarget(videoTarget)
        }
    }
}
