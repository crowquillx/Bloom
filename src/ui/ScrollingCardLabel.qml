import QtQuick

import BloomUI

Item {
    id: scrollingLabel

    property string text: ""
    property color textColor: Theme.textPrimary
    property int fontPixelSize: Theme.fontSizeSmall
    property string fontFamily: Theme.fontPrimary
    property int fontWeight: Font.DemiBold
    property real fontLetterSpacing: 0
    property int textStyle: Text.Normal
    property color textStyleColor: "transparent"
    property bool active: false
    property int gap: Math.round(fontPixelSize * 2.25)

    implicitHeight: staticLabel.implicitHeight
    clip: true

    readonly property real overflowWidth: Math.max(0, staticLabel.implicitWidth - width)
    readonly property bool shouldScroll: active && overflowWidth > 0

    Text {
        id: staticLabel
        anchors.horizontalCenter: parent.horizontalCenter
        visible: !scrollingLabel.shouldScroll
        text: scrollingLabel.text
        font.pixelSize: scrollingLabel.fontPixelSize
        font.family: scrollingLabel.fontFamily
        font.weight: scrollingLabel.fontWeight
        font.letterSpacing: scrollingLabel.fontLetterSpacing
        color: scrollingLabel.textColor
        style: scrollingLabel.textStyle
        styleColor: scrollingLabel.textStyleColor
        wrapMode: Text.NoWrap
    }

    Item {
        anchors.fill: parent
        visible: scrollingLabel.shouldScroll
        clip: true

        Item {
            id: marqueeTrack
            x: 0
            width: marqueePrimary.implicitWidth + scrollingLabel.gap + marqueeSecondary.implicitWidth
            height: parent.height

            Text {
                id: marqueePrimary
                text: scrollingLabel.text
                font.pixelSize: scrollingLabel.fontPixelSize
                font.family: scrollingLabel.fontFamily
                font.weight: scrollingLabel.fontWeight
                font.letterSpacing: scrollingLabel.fontLetterSpacing
                color: scrollingLabel.textColor
                style: scrollingLabel.textStyle
                styleColor: scrollingLabel.textStyleColor
                wrapMode: Text.NoWrap
            }

            Text {
                id: marqueeSecondary
                x: marqueePrimary.implicitWidth + scrollingLabel.gap
                text: scrollingLabel.text
                font.pixelSize: scrollingLabel.fontPixelSize
                font.family: scrollingLabel.fontFamily
                font.weight: scrollingLabel.fontWeight
                font.letterSpacing: scrollingLabel.fontLetterSpacing
                color: scrollingLabel.textColor
                style: scrollingLabel.textStyle
                styleColor: scrollingLabel.textStyleColor
                wrapMode: Text.NoWrap
            }
            SequentialAnimation on x {
                running: scrollingLabel.shouldScroll
                loops: Animation.Infinite

                PauseAnimation { duration: 1200 }
                NumberAnimation {
                    to: -(marqueePrimary.implicitWidth + scrollingLabel.gap)
                    duration: Math.max(2600, marqueePrimary.implicitWidth * 18)
                    easing.type: Easing.Linear
                }
                ScriptAction { script: marqueeTrack.x = 0 }
            }
        }
    }
}
