import QtQuick

import BloomUI

// Shared text-field / input surface. Assign to TextField.background and pass
// the control so focus draws the shared ring.
Rectangle {
    id: root

    property Item control: null
    readonly property bool focused: control ? control.activeFocus : false
    property bool error: false

    implicitHeight: Theme.buttonHeightMedium
    radius: Theme.radiusControl
    color: focused ? Theme.surfaceInteractiveHover : Theme.surfaceInteractive
    border.width: Theme.borderWidth
    border.color: error ? Theme.errorColor : Theme.surfaceBorder
    opacity: control && !control.enabled ? Theme.disabledOpacity : 1.0
    antialiasing: true

    FocusRing {
        active: root.focused
        radius: root.radius
        color: root.error ? Theme.errorColor : Theme.focusRing
    }
}
