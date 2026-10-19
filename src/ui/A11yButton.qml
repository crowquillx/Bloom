import QtQuick
import QtQuick.Controls

import BloomUI

/**
 * A11yButton - A Button wrapper that enforces accessibility properties and
 * draws Bloom's shared button surface and focus ring. Icon-only usage sets
 * font.family to the icon font and text to a glyph.
 */
Button {
    id: control
    
    // Accessibility properties
    property string a11yName: ""
    property string a11yDescription: ""
    property string toolTipText: ""
    property bool destructive: false

    signal activated()
    
    Accessible.role: Accessible.Button
    Accessible.name: a11yName || toolTipText || text
    Accessible.description: a11yDescription
    Accessible.focusable: true

    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
    ToolTip.delay: 500

    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: Theme.spacingSmall
    leftPadding: Theme.spacingMedium
    rightPadding: Theme.spacingMedium
    implicitHeight: Theme.buttonHeightMedium
    implicitWidth: Math.max(implicitHeight, implicitContentWidth + leftPadding + rightPadding)
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    background: Rectangle {
        id: buttonBackground
        radius: Theme.radiusControl
        color: {
            if (control.destructive)
                return control.down || control.hovered || control.activeFocus
                        ? Theme.actionDestructiveBackgroundHover : Theme.actionDestructiveBackground
            if (control.down) return Theme.surfaceInteractivePressed
            if (control.hovered || control.activeFocus) return Theme.surfaceInteractiveHover
            return Theme.surfaceInteractive
        }

        FocusRing {
            active: control.activeFocus
            radius: buttonBackground.radius
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.destructive ? Theme.actionDestructiveText : Theme.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    onClicked: activated()

    Keys.onReturnPressed: function(event) {
        if (event.isAutoRepeat)
            return
        activated()
        event.accepted = true
    }
    Keys.onEnterPressed: function(event) {
        if (event.isAutoRepeat)
            return
        activated()
        event.accepted = true
    }
    Keys.onSpacePressed: function(event) {
        if (event.isAutoRepeat)
            return
        activated()
        event.accepted = true
    }
}
