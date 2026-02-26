import QtQuick
import BloomUI

Item {
    id: root

    property Flickable target: null
    property real stepPx: Math.round(120 * Theme.layoutScale)
    property real pixelDeltaMultiplier: 1.0

    function scrollBy(deltaPx) {
        if (!target) {
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
            if (!root.target || !root.target.interactive) {
                return
            }
            if (wheel.modifiers & Qt.ControlModifier) {
                return
            }

            var angleY = wheel.angleDelta.y
            if (angleY !== 0) {
                // High-resolution wheels can emit partial notch values (e.g. 60).
                // Apply proportional movement so each event scrolls immediately.
                root.scrollBy(-(angleY / 120.0) * root.stepPx)
                wheel.accepted = true
                return
            }

            var pixelY = wheel.pixelDelta.y
            if (pixelY !== 0) {
                root.scrollBy(-pixelY * root.pixelDeltaMultiplier)
                wheel.accepted = true
            }
        }
    }
}
