import QtQuick
import QtQuick.Controls

/**
 * UnwatchedBadge - Displays unwatched episode count or watched checkmark
 * 
 * Shows as a corner fold/ribbon in top-right of series/season posters:
 * - When count > 0: displays the number of unwatched episodes
 * - When count == 0 && isFullyWatched: displays a checkmark icon
 * - Otherwise: hidden
 * 
 * Usage:
 *   UnwatchedBadge {
 *       anchors.top: parent.top
 *       anchors.right: parent.right
 *       parentWidth: parent.width
 *       count: modelData.UserData ? modelData.UserData.UnplayedItemCount : 0
 *       isFullyWatched: modelData.UserData ? modelData.UserData.Played : false
 *   }
 */
Item {
    id: root
    
    // Number of unwatched episodes
    property int count: 0
    // Whether the item is fully watched (all episodes played)
    property bool isFullyWatched: false
    // Width of the parent poster (used for proportional sizing)
    property real parentWidth: 100

    // Sizing
    readonly property real cornerSize: Math.max(32, Math.min(56, parentWidth * 0.25))
    readonly property real inset: cornerSize * 0.18
    readonly property bool shouldShow: count > 0 || isFullyWatched
    readonly property bool showCount: count > 0
    readonly property bool showCheckmark: count === 0 && isFullyWatched

    visible: shouldShow
    width: cornerSize
    height: cornerSize

    // Rounded badge (pill) inset from the corner so it stays within the clip
    Rectangle {
        id: badge
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: inset
        anchors.rightMargin: inset
        width: Math.max(cornerSize * 0.7, Math.max(countText.implicitWidth, checkIcon.implicitWidth) + cornerSize * 0.25)
        height: Math.max(cornerSize * 0.55, Math.max(countText.implicitHeight, checkIcon.implicitHeight) + cornerSize * 0.15)
        radius: height / 2
        color: Theme.accentSecondary
        antialiasing: true

        Text {
            id: countText
            visible: root.showCount
            text: root.count.toString()
            font.pixelSize: root.cornerSize * 0.32
            font.family: Theme.fontPrimary
            font.bold: true
            color: Theme.textPrimary
            anchors.centerIn: parent
        }

        Text {
            id: checkIcon
            visible: root.showCheckmark
            text: Icons.check
            font.pixelSize: root.cornerSize * 0.34
            font.family: Theme.fontIcon
            color: Theme.textPrimary
            anchors.centerIn: parent
        }
    }
}
