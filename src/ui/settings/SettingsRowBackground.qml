import QtQuick

import BloomUI

// Hover/focus surface behind a settings row. Extends slightly past the row
// so content keeps its alignment, and draws the focus ring inset so it is
// never clipped by the settings scroll area.
Rectangle {
    property bool focused: false
    property bool hovered: false

    anchors.fill: parent
    anchors.leftMargin: -Theme.spacingMedium
    anchors.rightMargin: -Theme.spacingMedium
    anchors.topMargin: -Theme.spacingSmall
    anchors.bottomMargin: -Theme.spacingSmall
    radius: Theme.radiusMedium
    color: focused ? Theme.surfaceInteractiveHover
         : (hovered ? Theme.surfaceInteractive : "transparent")

    Behavior on color {
        enabled: Theme.uiAnimationsEnabled
        ColorAnimation { duration: Theme.durationShort }
    }

    FocusRing {
        inset: true
        active: parent.focused
        radius: parent.radius
    }
}
