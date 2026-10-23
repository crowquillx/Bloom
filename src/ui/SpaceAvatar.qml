import QtQuick

import BloomUI

/**
 * SpaceAvatar - monogram tile for a Bloom space. A space with several
 * sources shows a second tile peeking out behind the first, so combined
 * spaces read differently from single ones without relying on color. The
 * peek width is always reserved, so rows of avatars stay aligned.
 */
Item {
    id: root

    property string name: ""
    property int sourceCount: 1
    /// The current space: selection fill and indicator border.
    property bool current: false
    property int size: Math.round(64 * Theme.layoutScale)

    readonly property int peek: Math.round(size * 0.14)

    implicitWidth: size + peek
    implicitHeight: size
    Accessible.ignored: true

    Rectangle {
        visible: root.sourceCount > 1
        x: root.peek
        y: Math.round(root.peek / 2)
        width: root.size
        height: root.size - root.peek
        radius: Theme.radiusMedium
        color: Theme.surfaceInteractive
        border.width: Theme.borderWidth
        border.color: Theme.surfaceBorderStrong
    }

    // Opaque base so the tile behind does not show through the tinted face.
    Rectangle {
        width: root.size
        height: root.size
        radius: Theme.radiusMedium
        color: Theme.backgroundPrimary
    }

    Rectangle {
        width: root.size
        height: root.size
        radius: Theme.radiusMedium
        color: root.current ? Theme.selectionBackground : Theme.surfaceInteractiveHover
        border.width: Theme.borderWidth
        border.color: root.current ? Theme.selectionIndicator : Theme.surfaceBorderStrong

        Text {
            anchors.centerIn: parent
            text: root.name !== "" ? root.name.charAt(0).toUpperCase() : "?"
            font.family: Theme.fontPrimary
            font.pixelSize: Math.round(root.size * 0.42)
            font.weight: Theme.fontWeightHeavy
            color: root.current ? Theme.selectionText : Theme.textPrimary
        }
    }
}
