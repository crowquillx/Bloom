import QtQuick

import BloomUI

// Hover/focus surface behind a settings row. Extends past the row's sides so
// labels and controls keep their alignment with the page, and draws the focus
// ring inset so it is never clipped by the settings scroll area. Rows carry
// their own vertical padding, so the surface matches their height.
Rectangle {
    property bool focused: false
    property bool hovered: false

    anchors.fill: parent
    anchors.leftMargin: -Theme.spacingMedium
    anchors.rightMargin: -Theme.spacingMedium
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
