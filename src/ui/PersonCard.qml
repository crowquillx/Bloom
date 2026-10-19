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

    scale: isFocused ? Theme.focusScale : 1.0
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
                source: {
                    const artwork = personCard.itemData.artwork
                    if (artwork && artwork.itemId) {
                        return LibraryService.getCachedArtworkUrlFromRef(artwork, 360)
                    }
                    return ""
                }
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

            ArtworkPlaceholder {
                anchors.fill: parent
                visible: personImage.status !== Image.Ready
                loading: personImage.status === Image.Loading
                iconGlyph: Icons.person
            }

            FocusRing {
                active: personCard.isFocused
                radius: Theme.imageRadius
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: castNameLabel.implicitHeight

            ScrollingCardLabel {
                id: castNameLabel
                anchors.fill: parent
                text: personCard.itemData.name || ""
                fontPixelSize: Theme.fontSizeSmall
                fontWeight: Theme.fontWeightStrong
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
                text: personCard.itemData.subtitle
                      || personCard.itemData.role
                      || personCard.itemData.kind
                      || ""
                fontPixelSize: Theme.fontSizeCaption
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
