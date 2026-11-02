pragma ComponentBehavior: Bound

import QtQuick
import BloomUI

// Sample cue drawn over a stand-in video frame with the mpv subtitle style
// an appearance gives under the default MPV profile
// (PlayerController.subtitlePreviewStyle, keyed by mpv property name), so a
// field left at Default shows the profile's font, size, colours, outline and
// shadow. Libraries or series with another profile can look different, and
// libass renders the real thing.
Item {
    id: root

    property var style: ({})
    property string sampleText: qsTr("I never said it would be easy.\nOnly that it would be worth it.")

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Subtitle preview")

    implicitHeight: Math.round(width * 9 / 16)

    FontLoader { id: gandhiRegular; source: "qrc:/mpv-fonts/GandhiSans-Regular.otf" }
    FontLoader { id: gandhiBold; source: "qrc:/mpv-fonts/GandhiSans-Bold.otf" }

    function number(name, fallback) {
        var value = parseFloat(root.style[name])
        return isNaN(value) ? fallback : value
    }
    // mpv colours are "#RRGGBB" or "#AARRGGBB", as Qt reads them.
    function mpvColor(name, fallback) {
        var value = root.style[name]
        return (typeof value === "string" && (value.length === 7 || value.length === 9))
            ? value : fallback
    }

    readonly property string family: root.style["sub-font"] || "sans-serif"
    readonly property bool boxed: root.style["sub-border-style"] === "opaque-box"
    // mpv sizes text in pixels of a 720 px tall frame.
    readonly property real frameScale: height / 720
    readonly property real borderSize: Math.max(0, number("sub-border-size", 1.65)) * frameScale
    readonly property real outlineWidth: boxed ? 0 : borderSize
    readonly property real shadowOffset: boxed ? 0 : Math.max(0, number("sub-shadow-offset", 0)) * frameScale
    // Directions the outline is stamped in; the radius follows its size.
    readonly property int outlineSteps: 16

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: Theme.radiusMedium
        clip: true
        border.width: Theme.borderWidth
        border.color: Theme.playbackControlGlassBorder
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.subtitlePreviewBackdropTop }
            GradientStop { position: 1.0; color: Theme.subtitlePreviewBackdropBottom }
        }

        Item {
            id: cue
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            // mpv's default bottom margin (sub-margin-y 34 of 720), raised by
            // sub-pos (100 is the bottom).
            anchors.bottomMargin: Math.round(34 * root.frameScale
                                             + (100 - root.number("sub-pos", 100)) / 100 * frame.height)
            width: frame.width - 2 * Theme.spacingMedium
            height: sample.contentHeight

            Rectangle {
                readonly property int padding: Math.round(root.borderSize)
                visible: root.boxed
                anchors.horizontalCenter: parent.horizontalCenter
                y: -padding
                width: sample.contentWidth + 2 * padding
                height: sample.contentHeight + 2 * padding
                color: root.mpvColor("sub-border-color", "#BF000000")
            }

            Text {
                visible: root.shadowOffset > 0
                x: sample.x + root.shadowOffset
                y: sample.y + root.shadowOffset
                width: sample.width
                text: sample.text
                font: sample.font
                wrapMode: sample.wrapMode
                horizontalAlignment: sample.horizontalAlignment
                color: root.mpvColor("sub-shadow-color", "#AF000000")
            }

            // The outline: the cue stamped around a circle as wide as the
            // border, so Thin, Medium and Thick differ as they do in mpv.
            Repeater {
                model: root.outlineWidth > 0 ? root.outlineSteps : 0
                Text {
                    required property int index
                    readonly property real angle: 2 * Math.PI * index / root.outlineSteps
                    x: sample.x + root.outlineWidth * Math.cos(angle)
                    y: sample.y + root.outlineWidth * Math.sin(angle)
                    width: sample.width
                    text: sample.text
                    font: sample.font
                    wrapMode: sample.wrapMode
                    horizontalAlignment: sample.horizontalAlignment
                    color: root.mpvColor("sub-border-color", "#FF000000")
                }
            }

            Text {
                id: sample
                width: parent.width
                text: root.sampleText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: root.mpvColor("sub-color", "#FFFFFFFF")
                font.pixelSize: Math.max(1, Math.round(root.number("sub-font-size", 38)
                                                       * root.number("sub-scale", 1) * root.frameScale))
                font.bold: root.family === "Gandhi Sans Bold"
                font.family: root.family.indexOf("Gandhi Sans") === 0
                    ? (gandhiRegular.name || "sans-serif") : root.family
            }
        }

        Text {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: Theme.spacingSmall
            text: root.style.profileName
                ? qsTr("Preview · %1 profile").arg(root.style.profileName) : qsTr("Preview")
            color: Theme.playbackTimeSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
        }
    }
}
