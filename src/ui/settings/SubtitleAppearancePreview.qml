import QtQuick
import BloomUI

// Sample cue drawn with a subtitle appearance map
// (ConfigManager.subtitleAppearance) over a stand-in video frame. It follows
// the mpv mapping in SubtitleAppearancePolicy closely enough to judge size,
// colours, box and position; libass renders the real thing.
Item {
    id: root

    property var appearance: ({})
    property string sampleText: qsTr("I never said it would be easy.\nOnly that it would be worth it.")

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Subtitle preview")

    implicitHeight: Math.round(width * 9 / 16)

    FontLoader { id: gandhiRegular; source: "qrc:/mpv-fonts/GandhiSans-Regular.otf" }
    FontLoader { id: gandhiBold; source: "qrc:/mpv-fonts/GandhiSans-Bold.otf" }

    function colorFor(hex, opacityPercent) {
        var value = (typeof hex === "string" && hex.length === 7) ? hex : "#FFFFFF"
        return Qt.rgba(parseInt(value.substr(1, 2), 16) / 255,
                       parseInt(value.substr(3, 2), 16) / 255,
                       parseInt(value.substr(5, 2), 16) / 255,
                       opacityPercent / 100)
    }

    readonly property string fontId: root.appearance.font || "default"
    readonly property bool boxed: root.appearance.background === "box"
    readonly property real sizeFactor: (root.appearance.sizePercent || 100) / 100
    // mpv sizes text in pixels of a 720 px tall frame (sub-font-size 38).
    readonly property real frameScale: height / 720
    readonly property real outlineWidth: {
        if (boxed) return 0
        switch (root.appearance.outline) {
        case "off": return 0
        case "thin": return 1.2 * frameScale
        case "medium": return 2.4 * frameScale
        case "thick": return 3.6 * frameScale
        default: return 1.65 * frameScale
        }
    }
    readonly property real shadowOffset: {
        if (boxed) return 0
        switch (root.appearance.shadow) {
        case "soft": return Math.max(1, 0.75 * frameScale)
        case "medium": return Math.max(1, 1.5 * frameScale)
        case "strong": return Math.max(1, 2.5 * frameScale)
        default: return 0
        }
    }

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
            // the position in % of the frame height (sub-pos).
            anchors.bottomMargin: Math.round(34 * root.frameScale
                                             + (root.appearance.positionPercent || 0) / 100 * frame.height)
            width: frame.width - 2 * Theme.spacingMedium
            height: sample.contentHeight

            Rectangle {
                readonly property int padding: Math.round(4 * root.frameScale)
                visible: root.boxed
                anchors.horizontalCenter: parent.horizontalCenter
                y: -padding
                width: sample.contentWidth + 2 * padding
                height: sample.contentHeight + 2 * padding
                color: root.colorFor(root.appearance.backgroundColor, root.appearance.backgroundOpacity || 0)
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
                color: Qt.rgba(0, 0, 0, 0.75)
            }

            Text {
                id: sample
                width: parent.width
                text: root.sampleText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: root.colorFor(root.appearance.textColor, 100)
                style: root.outlineWidth > 0 ? Text.Outline : Text.Normal
                styleColor: root.colorFor(root.appearance.outlineColor, 100)
                font.pixelSize: Math.max(1, Math.round(38 * root.sizeFactor * root.frameScale))
                font.bold: root.fontId === "gandhi-sans-bold"
                font.family: {
                    switch (root.fontId) {
                    case "gandhi-sans":
                    case "gandhi-sans-bold": return gandhiRegular.name || "sans-serif"
                    case "serif": return "serif"
                    case "monospace": return "monospace"
                    case "arial": return "Arial"
                    case "verdana": return "Verdana"
                    default: return "sans-serif"
                    }
                }
            }
        }

        Text {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: Theme.spacingSmall
            text: qsTr("Preview")
            color: Theme.playbackTimeSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
        }
    }
}
