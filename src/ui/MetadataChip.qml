import QtQuick

import BloomUI

Item {
    id: metadataChip

    property string text: ""

    implicitHeight: visualRect.implicitHeight
    implicitWidth: visualRect.implicitWidth
    visible: text !== ""

    Rectangle {
        id: visualRect
        anchors.fill: parent
        implicitHeight: Math.round(38 * Theme.layoutScale)
        implicitWidth: chipText.implicitWidth + Math.round(22 * Theme.layoutScale)
        radius: implicitHeight / 2
        color: Theme.chipBackground
        border.width: 1
        border.color: Theme.chipBorder

        Text {
            id: chipText
            anchors.centerIn: parent
            text: metadataChip.text
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            font.weight: Font.DemiBold
            color: Theme.textPrimary
        }
    }
}
