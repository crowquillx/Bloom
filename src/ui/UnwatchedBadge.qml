import QtQuick

import BloomUI

/**
 * UnwatchedBadge - unwatched episode count or watched check on artwork.
 *
 * Shows as a pill in the top-right of series/season posters:
 * - When count > 0: displays the number of unwatched episodes
 * - When count == 0 && isFullyWatched: displays a checkmark icon
 * - Otherwise: hidden
 *
 * Usage:
 *   UnwatchedBadge {
 *       anchors.top: parent.top
 *       anchors.right: parent.right
 *       parentWidth: parent.width
 *       count: modelData.unplayedItemCount || 0
 *       isFullyWatched: modelData.watched || false
 *   }
 */
Item {
    id: root

    // Number of unwatched episodes
    property int count: 0
    // Whether the item is fully watched (all episodes played)
    property bool isFullyWatched: false
    // Width of the parent poster (kept for API compatibility)
    property real parentWidth: 100

    readonly property bool shouldShow: count > 0 || isFullyWatched
    readonly property bool showCount: count > 0
    readonly property bool showCheckmark: count === 0 && isFullyWatched
    readonly property int inset: Math.round(12 * Theme.layoutScale)

    visible: shouldShow
    width: badge.width + inset * 2
    height: badge.height + inset * 2

    // Accent pill with on-accent text: readable on any artwork and theme.
    Rectangle {
        id: badge
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: root.inset
        anchors.rightMargin: root.inset
        height: Math.round(36 * Theme.layoutScale)
        width: Math.max(height, (root.showCount ? countText.implicitWidth : checkIcon.implicitWidth)
                                + Math.round(20 * Theme.layoutScale))
        radius: height / 2
        color: Theme.selectionIndicator
        border.width: Theme.borderWidth
        border.color: Theme.withAlpha(Theme.textOnAccent, 0.25)
        antialiasing: true

        Text {
            id: countText
            visible: root.showCount
            text: root.count.toString()
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightHeavy
            color: Theme.textOnAccent
            anchors.centerIn: parent
        }

        Text {
            id: checkIcon
            visible: root.showCheckmark
            text: Icons.check
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontIcon
            color: Theme.textOnAccent
            anchors.centerIn: parent
        }
    }
}
