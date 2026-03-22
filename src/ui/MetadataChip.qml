import QtQuick

import BloomUI

Rectangle {
    property string text: ""

    implicitHeight: Math.round(38 * Theme.layoutScale)
    implicitWidth: chipText.implicitWidth + Math.round(22 * Theme.layoutScale)
    radius: implicitHeight / 2
    color: Qt.rgba(0, 0, 0, 0.28)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.12)
    visible: text !== ""

    Text {
        id: chipText
        anchors.centerIn: parent
        text: parent.text
        font.pixelSize: Theme.fontSizeSmall
        font.family: Theme.fontPrimary
        font.weight: Font.DemiBold
        color: Theme.textPrimary
    }
}