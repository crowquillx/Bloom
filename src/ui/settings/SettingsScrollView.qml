pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import BloomUI

/**
 * SettingsScrollView - the scrolling content column of a settings page.
 *
 * Pages put their rows in a column inside it and call ensureFocusVisible(item)
 * when a row gains focus. It scrolls just enough to keep the row and a margin
 * of context in view, with an animation (instant under Reduce Motion), so the
 * focused row never sits against the clipped edge. Rows fade out at the
 * edges instead of being cut, so the column reads as part of the stage.
 */
Item {
    id: root

    /// The Flickable, for helpers that need one (WheelStepScroller).
    readonly property alias view: flick
    default property alias content: flick.flickableData
    property alias contentHeight: flick.contentHeight
    property alias contentY: flick.contentY
    /// Context kept above and below the focused row.
    property real focusMargin: Theme.navBarItemHeight
    /// Scrolled into the content; Settings recedes the navigation bar.
    readonly property bool scrolled: flick.contentY > Theme.spacingMedium

    readonly property real fadeSize: Theme.navBarItemHeight
    readonly property bool fadeTop: flick.contentY > 1
    readonly property bool fadeBottom: flick.contentY + flick.height < flick.contentHeight - 1

    function ensureFocusVisible(item) {
        if (!item || !item.visible)
            return
        var mapped = item.mapToItem(flick.contentItem, 0, 0)
        var top = mapped.y
        var bottom = mapped.y + item.height
        var maxY = Math.max(0, flick.contentHeight - flick.height)
        var current = scrollAnimation.running ? scrollAnimation.to : flick.contentY
        var target = current
        // Rows near the top bring the page back to its start, so group
        // headers and read-only rows above the first control stay reachable.
        if (top - focusMargin < current)
            target = top < flick.height / 2 ? 0 : top - focusMargin
        else if (bottom + focusMargin > current + flick.height)
            target = bottom + focusMargin - flick.height
        target = Math.max(0, Math.min(maxY, target))
        if (Math.abs(target - flick.contentY) < 1)
            return
        scrollAnimation.stop()
        scrollAnimation.from = flick.contentY
        scrollAnimation.to = target
        scrollAnimation.start()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        // A pointer drag or wheel takes over from a running focus scroll.
        onMovementStarted: scrollAnimation.stop()

        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software && (root.fadeTop || root.fadeBottom)
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: fadeMask
            // threshold 0.5 + spread 1.0 maps mask alpha 0..1 onto a smooth 0..1
            // ramp; the defaults would cut at the first clear pixel.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
    }

    // Only the alpha channel matters for the mask.
    Item {
        id: fadeMask
        anchors.fill: flick
        visible: false
        layer.enabled: true

        Rectangle {
            id: fadeRect
            anchors.fill: parent
            readonly property real edge: root.height > 0 ? Math.min(0.25, root.fadeSize / root.height) : 0
            readonly property color opaque: Theme.withAlpha(Theme.backgroundPrimary, 1.0)
            readonly property color clear: Theme.withAlpha(Theme.backgroundPrimary, 0.0)
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.fadeTop ? fadeRect.clear : fadeRect.opaque }
                GradientStop { position: fadeRect.edge; color: fadeRect.opaque }
                GradientStop { position: 1.0 - fadeRect.edge; color: fadeRect.opaque }
                GradientStop { position: 1.0; color: root.fadeBottom ? fadeRect.clear : fadeRect.opaque }
            }
        }
    }

    NumberAnimation {
        id: scrollAnimation
        target: flick
        property: "contentY"
        duration: Theme.durationNormal
        easing.type: Theme.easingStandard
    }
}
