import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

Button {
    id: actionButton

    property string iconGlyph: ""
    property color iconColor: Theme.textPrimary
    property bool showLabel: text !== ""
    property string accessibleLabel: ""
    property string toolTipText: ""

    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    implicitHeight: Theme.buttonHeightMedium
    implicitWidth: (!showLabel || text === "")
                   ? Theme.buttonHeightMedium
                   : buttonContent.implicitWidth + Math.round(34 * Theme.layoutScale)

    Accessible.role: Accessible.Button
    Accessible.name: accessibleLabel || text
    Accessible.focusable: true

    ToolTip.visible: hovered && enabled
                     && (toolTipText.length > 0
                         || (!showLabel && (accessibleLabel || text).length > 0))
    ToolTip.text: toolTipText || accessibleLabel || text
    ToolTip.delay: 500

    background: Rectangle {
        radius: Theme.radiusMedium
        color: {
            if (!actionButton.enabled) return Theme.buttonSecondaryBackgroundDisabled
            if (actionButton.down) return Theme.buttonSecondaryBackgroundPressed
            if (actionButton.hovered) return Theme.buttonSecondaryBackgroundHover
            return Theme.buttonSecondaryBackground
        }
        border.width: actionButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
        border.color: {
            if (actionButton.activeFocus) return Theme.buttonSecondaryBorderFocused
            if (actionButton.hovered) return Theme.buttonSecondaryBorderHover
            return Theme.buttonSecondaryBorder
        }

        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
    }

    contentItem: Item {
        id: buttonContent
        implicitWidth: buttonInnerRow.implicitWidth
        implicitHeight: buttonInnerRow.implicitHeight

        RowLayout {
            id: buttonInnerRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Text {
                text: actionButton.iconGlyph
                visible: text !== ""
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                color: actionButton.iconColor
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                text: actionButton.text
                visible: actionButton.showLabel && text !== ""
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                font.weight: Font.Black
                color: Theme.textPrimary
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
}
