import QtQuick

import BloomUI

// Check box indicator: accent fill with an on-accent check when checked, a
// quiet outlined square otherwise. Assign to CheckBox.indicator and pass the
// owning control so the indicator tracks its state.
Rectangle {
    id: root

    // CheckBox or CheckDelegate (var: qmllint cannot relate the styled types).
    property var control: null
    readonly property bool checked: control ? control.checked : false

    implicitWidth: Math.round(32 * Theme.layoutScale)
    implicitHeight: implicitWidth
    x: control ? control.leftPadding : 0
    y: control ? Math.round((control.height - height) / 2) : 0
    radius: Math.round(8 * Theme.layoutScale)
    color: checked ? Theme.selectionIndicator : Theme.surfaceInteractive
    border.width: checked ? 0 : Math.max(1, Math.round(2 * Theme.layoutScale))
    border.color: Theme.surfaceBorderStrong
    antialiasing: true

    Text {
        anchors.centerIn: parent
        visible: root.checked
        text: Icons.check
        font.family: Theme.fontIcon
        font.pixelSize: Math.round(root.height * 0.8)
        color: Theme.textOnAccent
    }
}
