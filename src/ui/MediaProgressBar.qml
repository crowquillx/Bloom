import QtQuick
import BloomUI

/**
 * MediaProgressBar - A reusable progress bar for media thumbnails
 * 
 * Shows playback progress as a horizontal bar at the bottom of media thumbnails.
 * Commonly used in Next Up, Season View, and anywhere else showing resumable content.
 * 
 * Usage:
 *   MediaProgressBar {
 *       anchors.left: parent.left
 *       anchors.right: parent.right
 *       anchors.bottom: parent.bottom
 *       positionTicks: modelData.UserData ? modelData.UserData.PlaybackPositionTicks : 0
 *       runtimeTicks: modelData.RunTimeTicks || 0
 *   }
 */
Item {
    id: root
    
    // Progress data (in Jellyfin ticks - 10,000,000 ticks = 1 second)
    property var positionTicks: 0
    property var runtimeTicks: 0
    
    // Visual properties
    property int barHeight: 4
    property int barMargin: 4
    property int barRadius: 2
    property color backgroundColor: Qt.rgba(0, 0, 0, 0.6)
    property color progressColor: Theme.accentPrimary
    
    // Computed progress (0.0 to 1.0)
    readonly property real progress: {
        if (!runtimeTicks || runtimeTicks <= 0 || !positionTicks || positionTicks <= 0) {
            return 0
        }
        return Math.min(1.0, positionTicks / runtimeTicks)
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
            width: parent.width * root.progress
            radius: root.barRadius
            color: root.progressColor
            
            Behavior on width {
                NumberAnimation { duration: Theme.durationShort }
            }
        }
    }
}
