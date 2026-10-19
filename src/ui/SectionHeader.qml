import QtQuick
import QtQuick.Layouts

import BloomUI

/**
 * SectionHeader - title for a shelf, grid or settings group, with optional
 * supporting text on the same baseline (e.g. an item count or hint).
 */
RowLayout {
    id: root

    property string text: ""
    property string detail: ""
    /// "page" for screen titles, "section" for shelves, "group" for settings.
    property string level: "section"

    spacing: Theme.spacingMedium

    Text {
        text: root.text
        font.pixelSize: root.level === "page" ? Theme.fontSizeDisplay
                       : (root.level === "group" ? Theme.fontSizeMedium : Theme.fontSizeHeader)
        font.family: Theme.fontPrimary
        font.weight: root.level === "group" ? Theme.fontWeightStrong : Theme.fontWeightHeavy
        color: Theme.textPrimary
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignBaseline
        Layout.fillWidth: root.detail === ""
    }

    Text {
        text: root.detail
        visible: text !== ""
        font.pixelSize: Theme.fontSizeSmall
        font.family: Theme.fontPrimary
        color: Theme.textSecondary
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignBaseline
        Layout.fillWidth: true
    }
}
