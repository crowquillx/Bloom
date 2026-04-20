import QtQuick
import QtQuick.Layouts
import BloomUI

RowLayout {
    id: root
    property string label: ""
    property string value: ""

    Accessible.role: Accessible.StaticText
    Accessible.name: label + ": " + value

    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    Text {
        text: root.label
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontPrimary
        color: Theme.textSecondary
    }

    Text {
        text: root.value
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontPrimary
        color: Theme.textPrimary
        elide: Text.ElideMiddle
        Layout.fillWidth: true
    }
}
