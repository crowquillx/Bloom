import QtQuick
import QtQuick.Layouts

import BloomUI

/**
 * PageHeader - title block for pages without a hero (libraries, search,
 * settings, sessions).
 *
 * An optional accent eyebrow, the page title in display type, and an optional
 * detail line (item count, source, hint). Trailing controls (filters, sort,
 * back) go in the default slot and sit on the title's baseline row.
 *
 *   PageHeader {
 *       eyebrow: qsTr("Library")
 *       title: "Movies"
 *       detail: qsTr("%1 titles").arg(count)
 *       ActionButton { text: qsTr("Sort") }
 *   }
 */
RowLayout {
    id: root

    property string eyebrow: ""
    property string title: ""
    property string detail: ""
    default property alias trailing: trailingRow.data

    spacing: Theme.spacingLarge

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignBottom
        spacing: Math.round(6 * Theme.layoutScale)

        Text {
            visible: text !== ""
            text: root.eyebrow
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
            font.weight: Theme.fontWeightHeavy
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.eyebrowLetterSpacing
            color: Theme.accentPrimary
            Accessible.ignored: true
        }

        Text {
            Layout.fillWidth: true
            text: root.title
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontSizeDisplay
            font.weight: Theme.fontWeightHeavy
            font.letterSpacing: Theme.displayLetterSpacing
            color: Theme.textPrimary
            elide: Text.ElideRight
            maximumLineCount: 1
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Text {
            Layout.fillWidth: true
            visible: text !== ""
            text: root.detail
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Theme.fontWeightMedium
            color: Theme.textSecondary
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    RowLayout {
        id: trailingRow
        Layout.alignment: Qt.AlignBottom
        spacing: Theme.controlSpacing
    }
}
