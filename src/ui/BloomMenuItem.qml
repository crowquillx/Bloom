import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

// Menu row: large readable label, optional icon, focus drawn as an inset ring
// so it is never clipped by the menu surface.
MenuItem {
    id: item

    property string iconGlyph: ""
    property bool destructive: false
    /// Shows the check mark without making the item checkable (for options
    /// whose state is bound elsewhere).
    property bool marked: false

    implicitHeight: Theme.buttonHeightMedium
    indicator: Item {}
    leftPadding: Theme.spacingMedium
    rightPadding: Theme.spacingMedium
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    contentItem: RowLayout {
        spacing: Theme.spacingMedium

        Text {
            visible: item.iconGlyph !== ""
            text: item.iconGlyph
            font.family: Theme.fontIcon
            font.pixelSize: Theme.fontSizeIcon
            color: item.destructive ? Theme.errorColor : Theme.textSecondary
            Layout.alignment: Qt.AlignVCenter
        }

        // Long labels (track names) scroll while highlighted.
        ScrollingCardLabel {
            text: item.text
            horizontalAlignment: Text.AlignLeft
            fontPixelSize: Theme.fontSizeBody
            fontWeight: item.highlighted ? Theme.fontWeightStrong : Theme.fontWeightRegular
            textColor: item.destructive ? Theme.errorColor : Theme.textPrimary
            active: item.highlighted
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            visible: item.checkable || item.marked
            text: item.checked || item.marked ? Icons.check : ""
            font.family: Theme.fontIcon
            font.pixelSize: Theme.fontSizeIcon
            color: Theme.selectionIndicator
            Layout.alignment: Qt.AlignVCenter
        }
    }

    background: Rectangle {
        implicitWidth: Math.round(360 * Theme.layoutScale)
        radius: Theme.radiusMedium
        color: item.down ? Theme.surfaceInteractivePressed
             : (item.highlighted ? Theme.surfaceInteractiveHover : "transparent")

        FocusRing {
            inset: true
            active: item.highlighted && item.activeFocus
            radius: parent.radius
        }
    }
}
