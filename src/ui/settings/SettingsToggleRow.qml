import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    property string label: ""
    property string description: ""
    property bool checked: false
    property bool isHovered: rowClickArea.containsMouse
    property bool hasKeyboardFocus: root.activeFocus
    property var ensureVisible: null

    Accessible.role: Accessible.CheckBox
    Accessible.name: label
    Accessible.description: description
    Accessible.checkable: true
    Accessible.checked: checked

    signal toggled(bool value)

    implicitHeight: content.implicitHeight
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

    SettingsRowBackground {
        focused: root.hasKeyboardFocus
        hovered: root.isHovered
    }

    MouseArea {
        id: rowClickArea
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggle()
    }

    RowLayout {
        id: content
        width: parent.width
        spacing: Theme.spacingMedium

        ColumnLayout {
            spacing: Math.round(4 * Theme.layoutScale)
            Layout.fillWidth: true

            Text {
                text: root.label
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
            }

            Text {
                text: root.description
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                visible: text.length > 0
            }
        }

        Rectangle {
            id: switchTrack
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            width: Math.round(68 * Theme.layoutScale)
            height: Math.round(38 * Theme.layoutScale)
            radius: height / 2
            color: root.checked ? Theme.selectionIndicator : Theme.surfaceInteractivePressed
            border.width: root.checked ? 0 : Theme.borderWidth
            border.color: Theme.surfaceBorderStrong

            Behavior on color { ColorAnimation { duration: Theme.durationShort } }

            Rectangle {
                readonly property int knobInset: Math.round(4 * Theme.layoutScale)
                width: parent.height - knobInset * 2
                height: width
                radius: width / 2
                x: root.checked ? parent.width - width - knobInset : knobInset
                anchors.verticalCenter: parent.verticalCenter
                color: root.checked ? Theme.textOnAccent : Theme.textPrimary

                Behavior on x { enabled: Theme.uiAnimationsEnabled; NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.enabled
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggle()
            }
        }
    }

}
