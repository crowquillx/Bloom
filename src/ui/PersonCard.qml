import QtQuick
import QtQuick.Layouts
import QtQuick.Effects

import BloomUI

FocusScope {
    id: personCard

    required property var itemData
    property bool isFocused: false
    property bool isHovered: InputModeManager.pointerActive && personMouseArea.containsMouse
    readonly property int posterFrameWidth: width
    readonly property int posterFrameHeight: Math.round(posterFrameWidth * 1.5)

    implicitWidth: Math.round(176 * Theme.layoutScale)
    implicitHeight: Math.round(320 * Theme.layoutScale)

    scale: isFocused ? 1.04 : (isHovered ? 1.02 : 1.0)
    transformOrigin: Item.Center
    Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: personCard.posterFrameHeight
            radius: Theme.imageRadius
            color: "transparent"
            clip: false

            Image {
                id: personImage
                anchors.fill: parent
                source: personCard.itemData.Id && personCard.itemData.PrimaryImageTag
                        ? LibraryService.getCachedImageUrlWithWidth(personCard.itemData.Id, "Primary", 360)
                        : ""
                fillMode: Image.PreserveAspectCrop
                horizontalAlignment: Image.AlignHCenter
                verticalAlignment: Image.AlignBottom
                asynchronous: true
                cache: true

                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: personImageMask
                }
            }

            Item {
                id: personImageMask
                anchors.fill: parent
                visible: false
                layer.enabled: true
                layer.smooth: true

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.imageRadius
                    color: "white"
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.imageRadius
                color: Qt.rgba(0.08, 0.08, 0.08, 0.45)
                visible: personImage.status !== Image.Ready

                Text {
                    anchors.centerIn: parent
                    text: Icons.person
                    font.family: Theme.fontIcon
                    font.pixelSize: Math.round(56 * Theme.layoutScale)
                    color: Theme.textSecondary
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.imageRadius
                color: "transparent"
                border.width: personCard.isFocused ? Theme.buttonFocusBorderWidth : 0
                border.color: Theme.accentPrimary
                visible: border.width > 0
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: castNameLabel.implicitHeight

            ScrollingCardLabel {
                id: castNameLabel
                anchors.fill: parent
                text: personCard.itemData.Name || ""
                fontPixelSize: Theme.fontSizeSmall
                fontWeight: Font.DemiBold
                textColor: Theme.textPrimary
                active: personCard.isFocused
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: castSubtitleLabel.implicitHeight
            visible: castSubtitleLabel.text !== ""

            ScrollingCardLabel {
                id: castSubtitleLabel
                anchors.fill: parent
                text: personCard.itemData.Subtitle || ""
                fontPixelSize: Theme.fontSizeSmall
                fontWeight: Font.Normal
                textColor: Theme.textSecondary
                active: personCard.isFocused
            }
        }
    }

    MouseArea {
        id: personMouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: personCard.forceActiveFocus()
    }
}
