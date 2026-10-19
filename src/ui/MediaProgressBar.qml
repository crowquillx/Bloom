import QtQuick
import BloomUI

/**
 * MediaProgressBar - playback progress drawn along the bottom of artwork.
 *
 * Shows playback progress as a rounded line inset from the card edges.
 * Commonly used in Continue Watching, Season View, and anywhere else showing
 * resumable content.
 *
 * Usage:
 *   MediaProgressBar {
 *       anchors.left: parent.left
 *       anchors.right: parent.right
 *       anchors.bottom: parent.bottom
 *       positionMs: modelData.positionMs || 0
 *       durationMs: modelData.durationMs || 0
 *   }
 */
Item {
    id: root

    // Canonical progress data in milliseconds.
    property real positionMs: 0
    property real durationMs: 0

    // Visual properties
    property int barHeight: Theme.progressHeight
    property int barMargin: Math.round(14 * Theme.layoutScale)
    property int barRadius: Math.round(barHeight / 2)
    property color backgroundColor: Theme.progressTrackOnArtwork
    property color progressColor: Theme.progressFill

    // Computed progress (0.0 to 1.0)
    readonly property real progress: {
        if (durationMs <= 0 || positionMs <= 0) {
            return 0
        }
        return Math.min(1.0, positionMs / durationMs)
    }

    // Only visible when there's actual progress
    visible: progress > 0

    height: barHeight + (barMargin * 2)

    // Background track
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: root.barMargin
        height: root.barHeight
        radius: root.barRadius
        color: root.backgroundColor

        // Progress fill
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.max(height, parent.width * root.progress)
            radius: root.barRadius
            color: root.progressColor

            Behavior on width {
                enabled: Theme.uiAnimationsEnabled
                NumberAnimation { duration: Theme.durationShort }
            }
        }
    }
}
