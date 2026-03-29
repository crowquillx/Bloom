import QtQuick
import BloomUI

Item {
    id: root

    property bool shrinkEnabled: PlayerController.embeddedVideoShrinkEnabled

    // Prevent bright flashes before the first frame by using themed dark backdrops.
    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundPrimary
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.10) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.28) }
        }
    }

    Loader {
        id: videoTargetLoader
        anchors.fill: parent
        active: root.visible

        sourceComponent: Component {
            MpvVideoItem {
                id: videoTarget
                anchors {
                    right: parent.right
                    bottom: parent.bottom
                    rightMargin: root.shrinkEnabled ? Theme.spacingLarge : 0
                    bottomMargin: root.shrinkEnabled ? Theme.spacingLarge : 0
                }
                width: root.shrinkEnabled ? parent.width * 0.72 : parent.width
                height: root.shrinkEnabled ? (parent.height * 0.72) : parent.height

                onViewportChanged: function(x, y, width, height) {
                    PlayerController.setEmbeddedVideoViewport(x, y, width, height)
                }

                function syncEmbeddedViewport() {
                    if (width > 0 && height > 0) {
                        var topLeft = videoTarget.mapToItem(null, 0, 0)
                        PlayerController.setEmbeddedVideoViewport(topLeft.x, topLeft.y, width, height)
                    }
                }

                onWidthChanged: syncEmbeddedViewport()
                onHeightChanged: syncEmbeddedViewport()

                Component.onCompleted: {
                    PlayerController.attachEmbeddedVideoTarget(videoTarget)
                    if (width > 0 && height > 0) {
                        syncEmbeddedViewport()
                    } else {
                        Qt.callLater(syncEmbeddedViewport)
                    }
                }

                Component.onDestruction: {
                    PlayerController.detachEmbeddedVideoTarget(videoTarget)
                }
            }
        }
    }
}
