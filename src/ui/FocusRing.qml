import QtQuick

import BloomUI

/**
 * FocusRing - Bloom's keyboard/remote focus indicator.
 *
 * Draws a high-contrast outline around its parent, separated from it by a
 * small gap, with a contrasting halo so it stays visible over bright or busy
 * artwork. Focus is shape + contrast, never colour alone.
 *
 * Usage (as a child of the focusable item's visual):
 *   FocusRing { active: control.activeFocus; radius: background.radius }
 *
 * Set `inset: true` inside clipped containers (lists, popups) to draw the ring
 * on the item's own edge instead of outside it.
 */
Item {
    id: root

    property bool active: false
    /// Corner radius of the decorated item; the ring follows it outwards.
    property real radius: Theme.radiusControl
    property bool inset: false
    property color color: Theme.focusRing
    property color haloColor: Theme.focusRingHalo
    property int ringWidth: Theme.focusRingWidth
    property int gap: inset ? 0 : Theme.focusRingGap

    readonly property real outset: inset ? 0 : gap + ringWidth
    readonly property int haloWidth: Math.max(1, Math.round(2 * Theme.layoutScale))

    anchors.fill: parent
    anchors.margins: -outset
    z: 50
    visible: opacity > 0
    opacity: active ? 1.0 : 0.0
    enabled: false

    Behavior on opacity {
        enabled: Theme.uiAnimationsEnabled
        NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutQuad }
    }

    // Halo outside the ring separates it from light artwork (dark themes) or
    // dark artwork (light themes).
    Rectangle {
        anchors.fill: parent
        anchors.margins: root.inset ? root.ringWidth : -root.haloWidth
        radius: root.inset ? Math.max(0, root.radius - root.ringWidth)
                           : (root.radius > 0 ? root.radius + root.outset + root.haloWidth : 0)
        color: "transparent"
        border.width: root.haloWidth
        border.color: root.haloColor
        antialiasing: true
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius > 0 ? root.radius + root.outset : 0
        color: "transparent"
        border.width: root.ringWidth
        border.color: root.color
        antialiasing: true
    }
}
