import QtQuick

import BloomUI

/**
 * ArtworkPlaceholder - intentional fallback for missing or loading artwork.
 *
 * Shows a theme-tinted rounded surface with a media-type glyph and, when the
 * artwork is missing (not just loading), the item title so cards stay
 * identifiable without art.
 */
Rectangle {
    id: root

    property string iconGlyph: Icons.movie
    property string title: ""
    property bool loading: false

    radius: Theme.radiusArtwork
    color: Theme.artworkPlaceholder
    antialiasing: true

    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.mix(Theme.artworkPlaceholder, Theme.accentPrimary, 0.10) }
        GradientStop { position: 1.0; color: Theme.artworkPlaceholder }
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - Theme.spacingLarge
        spacing: Theme.spacingSmall
        opacity: root.loading ? 0.5 : 1.0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.iconGlyph
            font.family: Theme.fontIcon
            font.pixelSize: Math.max(Theme.fontSizeIcon, Math.round(Math.min(root.width, root.height) * 0.22))
            color: Theme.artworkPlaceholderIcon
        }

        Text {
            width: parent.width
            visible: !root.loading && text !== ""
            text: root.title
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightStrong
            color: Theme.textSecondary
        }
    }
}
