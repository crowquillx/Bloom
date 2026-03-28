import QtQuick
import QtQuick.Controls

/**
 * A11yButton - A Button wrapper that enforces accessibility properties.
 */
Button {
    id: control
    
    // Accessibility properties
    property string a11yName: ""
    property string a11yDescription: ""
    property string toolTipText: ""

    signal activated()
    
    Accessible.role: Accessible.Button
    Accessible.name: a11yName || text
    Accessible.description: a11yDescription
    Accessible.focusable: true

    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText

    Keys.onReturnPressed: function(event) {
        if (event.isAutoRepeat)
            return
        activated()
        clicked()
        event.accepted = true
    }
    Keys.onEnterPressed: function(event) {
        if (event.isAutoRepeat)
            return
        activated()
        clicked()
        event.accepted = true
    }
    Keys.onSpacePressed: function(event) {
        if (event.isAutoRepeat)
            return
        activated()
        clicked()
        event.accepted = true
    }
}
