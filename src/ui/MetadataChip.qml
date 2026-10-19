import QtQuick

import BloomUI

// Compact metadata label. Borderless and low-fill so groups of facts read as
// text rather than a row of boxes; prefer MetadataLine for plain fact lists.
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
        implicitHeight: Math.round(40 * Theme.layoutScale)
        implicitWidth: chipText.implicitWidth + Math.round(28 * Theme.layoutScale)
        radius: Theme.radiusSmall
        color: metadataChip.onArtwork ? Theme.overlayTextBackground : Theme.surfaceInteractive
        border.width: metadataChip.onArtwork ? 1 : 0
        border.color: Theme.overlayTextBorder

        Text {
            id: chipText
            anchors.centerIn: parent
            text: metadataChip.text
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightMedium
            color: metadataChip.onArtwork ? Theme.textOnDarkOverlay : Theme.textPrimary
        }
    }
}
