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
    
    Accessible.role: Accessible.Button
    Accessible.name: a11yName || text
    Accessible.description: a11yDescription
    Accessible.focusable: true
}
