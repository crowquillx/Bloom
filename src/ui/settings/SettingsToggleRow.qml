import QtQuick
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    property string label: ""
    property string description: ""
    property bool checked: false
    property bool isHovered: frame.hovered
    property bool hasKeyboardFocus: root.activeFocus
    property var ensureVisible: null

    Accessible.role: Accessible.CheckBox
    Accessible.name: label
    Accessible.description: description
    Accessible.checkable: true
    Accessible.checked: checked

    signal toggled(bool value)

    implicitHeight: frame.implicitHeight
    Layout.fillWidth: true

    onActiveFocusChanged: {
        if (activeFocus && ensureVisible) ensureVisible(root)
    }

    Keys.onSpacePressed: function(event) { toggle(); event.accepted = true }
    Keys.onReturnPressed: function(event) { toggle(); event.accepted = true }
    Keys.onEnterPressed: function(event) { toggle(); event.accepted = true }

    function toggle() {
        if (!enabled) return
        toggled(!checked)
    }

    SettingsRow {
        id: frame
        anchors.left: parent.left
        anchors.right: parent.right
        label: root.label
        description: root.description
        highlighted: root.hasKeyboardFocus
        focusTarget: root
        onActivated: root.toggle()

        Rectangle {
            id: switchTrack
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            implicitWidth: Math.round(72 * Theme.layoutScale)
            implicitHeight: Math.round(40 * Theme.layoutScale)
            radius: height / 2
            color: root.checked ? Theme.selectionIndicator : Theme.surfaceInteractivePressed
            border.width: root.checked ? 0 : Theme.borderWidth
            border.color: Theme.surfaceBorderStrong
            opacity: root.enabled ? 1.0 : Theme.disabledOpacity

            Behavior on color { enabled: Theme.uiAnimationsEnabled; ColorAnimation { duration: Theme.durationShort } }

            Rectangle {
                readonly property int knobInset: Math.round(4 * Theme.layoutScale)
                width: parent.height - knobInset * 2
                height: width
                radius: width / 2
                x: root.checked ? parent.width - width - knobInset : knobInset
                anchors.verticalCenter: parent.verticalCenter
                color: root.checked ? Theme.textOnAccent : Theme.textPrimary

                Behavior on x { enabled: Theme.uiAnimationsEnabled; NumberAnimation { duration: Theme.durationShort; easing.type: Theme.easingStandard } }
            }
        }
    }
}
