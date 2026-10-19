import QtQuick
import QtQuick.Controls

import BloomUI

// Context menu with Bloom's floating surface. Pair with BloomMenuItem.
Menu {
    id: menu

    padding: Theme.spacingSmall
    // Keep long menus (track lists) inside the window; the list scrolls.
    margins: Theme.spacingLarge
    implicitWidth: Math.max(Math.round(380 * Theme.layoutScale), contentItem ? contentItem.implicitWidth + leftPadding + rightPadding : 0)

    background: DialogSurface {
        floating: true
        implicitWidth: Math.round(380 * Theme.layoutScale)
    }

    // Items created for sub-menus and actions use the same row style.
    delegate: BloomMenuItem {}

    Overlay.modal: ModalScrim { opacity: 0.4 }
    Overlay.modeless: Item {}

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durationShort }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Theme.durationShort }
    }
}
