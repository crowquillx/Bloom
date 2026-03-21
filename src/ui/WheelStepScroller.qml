import QtQuick
import BloomUI

Item {
    id: root

    property Flickable target: null
    property int orientation: Qt.Vertical
    property real stepPx: Math.round(120 * Theme.layoutScale)
    property real pixelDeltaMultiplier: 1.0

    function scrollBy(deltaPx) {
        if (!target) {
            return
        }

        if (orientation === Qt.Horizontal) {
            var maxX = Math.max(0, target.contentWidth - target.width)
            if (maxX <= 0) {
                return
            }

            target.contentX = Math.max(0, Math.min(maxX, target.contentX + deltaPx))
            return
        }

        var maxY = Math.max(0, target.contentHeight - target.height)
        if (maxY <= 0) {
            return
        }

        target.contentY = Math.max(0, Math.min(maxY, target.contentY + deltaPx))
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        enabled: root.enabled && root.visible && root.target && root.target.visible

        onWheel: function(wheel) {
            if (!root.target) {
                return
            }
            if (wheel.modifiers & Qt.ControlModifier) {
                return
            }

            var angleDelta = root.orientation === Qt.Horizontal
                    ? (wheel.angleDelta.x !== 0 ? wheel.angleDelta.x : wheel.angleDelta.y)
                    : (wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x)
            if (angleDelta !== 0) {
                // High-resolution wheels can emit partial notch values (e.g. 60).
                // Apply proportional movement so each event scrolls immediately.
                root.scrollBy(-(angleDelta / 120.0) * root.stepPx)
                wheel.accepted = true
                return
            }

            var pixelDelta = root.orientation === Qt.Horizontal
                    ? (wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x : wheel.pixelDelta.y)
                    : (wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.pixelDelta.x)
            if (pixelDelta !== 0) {
                root.scrollBy(-pixelDelta * root.pixelDeltaMultiplier)
                wheel.accepted = true
            }
        }
    }
}
