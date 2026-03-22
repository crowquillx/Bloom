import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

Button {
    id: actionButton

    property string iconGlyph: ""
    property color iconColor: Theme.textPrimary

    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    implicitHeight: Theme.buttonHeightMedium
    implicitWidth: text === ""
                   ? Theme.buttonHeightMedium
                   : buttonContent.implicitWidth + Math.round(34 * Theme.layoutScale)

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

    contentItem: RowLayout {
        id: buttonContent
        implicitWidth: buttonInnerRow.implicitWidth
        implicitHeight: buttonInnerRow.implicitHeight
        anchors.centerIn: parent

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
                visible: text !== ""
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
