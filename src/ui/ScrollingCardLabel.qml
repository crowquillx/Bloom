import QtQuick

import BloomUI

Item {
    id: scrollingLabel

    property string text: ""
    property color textColor: Theme.textPrimary
    property int fontPixelSize: Theme.fontSizeSmall
    property string fontFamily: Theme.fontPrimary
    property int fontWeight: Font.DemiBold
    property bool active: false

    implicitHeight: label.implicitHeight
    clip: true

    readonly property real overflowWidth: Math.max(0, label.implicitWidth - width)

    states: [
        State {
            name: "static"
            when: !labelScrollAnimation.running

            AnchorChanges {
                target: label
                anchors.left: scrollingLabel.overflowWidth > 0 ? scrollingLabel.left : undefined
                anchors.horizontalCenter: scrollingLabel.overflowWidth > 0 ? undefined : scrollingLabel.horizontalCenter
            }

            PropertyChanges {
                target: label
                x: 0
            }
        },
        State {
            name: "scrolling"
            when: labelScrollAnimation.running

            AnchorChanges {
                target: label
                anchors.left: undefined
                anchors.horizontalCenter: undefined
            }

            PropertyChanges {
                target: label
                x: 0
            }
        }
    ]

    Text {
        id: label
        text: scrollingLabel.text
        font.pixelSize: scrollingLabel.fontPixelSize
        font.family: scrollingLabel.fontFamily
        font.weight: scrollingLabel.fontWeight
        color: scrollingLabel.textColor
        wrapMode: Text.NoWrap
    }

    SequentialAnimation {
        id: labelScrollAnimation
        running: scrollingLabel.active && scrollingLabel.overflowWidth > 0
        loops: Animation.Infinite

        PauseAnimation { duration: 1000 }
        NumberAnimation {
            target: label
            property: "x"
            to: -scrollingLabel.overflowWidth
            duration: Math.max(1200, scrollingLabel.overflowWidth * 20)
            easing.type: Easing.Linear
        }
        PauseAnimation { duration: 1000 }
        NumberAnimation {
            target: label
            property: "x"
            to: 0
            duration: Math.max(1200, scrollingLabel.overflowWidth * 20)
            easing.type: Easing.Linear
        }
    }
}
