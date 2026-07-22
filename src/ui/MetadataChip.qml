import QtQuick

import BloomUI

Item {
    id: metadataChip

    property string text: ""
    // Use a dark scrim + light text when drawn over posters/artwork so badges
    // stay legible across themes and busy/light image regions.
    property bool onArtwork: false

    implicitHeight: visualRect.implicitHeight
    implicitWidth: visualRect.implicitWidth
    height: implicitHeight
    width: implicitWidth
    visible: text !== ""

    Rectangle {
        id: visualRect
        anchors.fill: parent
        implicitHeight: Math.round(38 * Theme.layoutScale)
        implicitWidth: chipText.implicitWidth + Math.round(22 * Theme.layoutScale)
        radius: implicitHeight / 2
        color: metadataChip.onArtwork ? Theme.overlayTextBackground : Theme.chipBackground
        border.width: 1
        border.color: metadataChip.onArtwork ? Theme.overlayTextBorder : Theme.chipBorder

        Text {
            id: chipText
            anchors.centerIn: parent
            text: metadataChip.text
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            font.weight: Font.DemiBold
            color: metadataChip.onArtwork ? Theme.textOnDarkOverlay : Theme.textPrimary
        }
    }
}
