import QtQuick

import BloomUI

// Names the server/account that owns an item in a merged workspace, so two
// users on one server stay distinguishable. Items from single workspaces
// carry no sourceLabel, which keeps the badge hidden.
Rectangle {
    id: sourceBadge

    property string text: ""
    property real maximumWidth: Math.round(260 * Theme.layoutScale)

    visible: text !== ""
    implicitHeight: Math.round(34 * Theme.layoutScale)
    implicitWidth: Math.min(maximumWidth, badgeLabel.implicitWidth + Math.round(20 * Theme.layoutScale))
    width: implicitWidth
    height: implicitHeight
    radius: Theme.radiusSmall
    color: Theme.overlayTextBackground
    border.width: Theme.borderWidth
    border.color: Theme.overlayTextBorder

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Source: %1").arg(text)

    Text {
        id: badgeLabel
        anchors.fill: parent
        anchors.leftMargin: Math.round(10 * Theme.layoutScale)
        anchors.rightMargin: Math.round(10 * Theme.layoutScale)
        text: sourceBadge.text
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontPrimary
        font.weight: Theme.fontWeightMedium
        color: Theme.textOnDarkOverlay
    }
}
