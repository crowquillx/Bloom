import QtQuick
import QtQuick.Layouts
import BloomUI

// Read-only "label ... value" row. Same rhythm as the interactive settings
// rows, without hover or focus treatment.
Item {
    id: root
    property string label: ""
    property string value: ""

    Accessible.role: Accessible.StaticText
    Accessible.name: label + ": " + value

    Layout.fillWidth: true
    implicitHeight: Math.max(Theme.navBarItemHeight, content.implicitHeight + Theme.spacingMedium)

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingLarge

        Text {
            text: root.label
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightStrong
            color: Theme.textPrimary
            Layout.alignment: Qt.AlignTop
        }

        Text {
            text: root.value
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignRight
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
        }
    }
}
