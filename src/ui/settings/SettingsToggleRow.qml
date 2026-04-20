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

    Rectangle {
        anchors.fill: parent
        anchors.margins: -Theme.spacingSmall
        radius: Theme.radiusSmall
        color: root.hasKeyboardFocus
            ? Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.16)
            : (root.isHovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
        border.color: root.hasKeyboardFocus
            ? Theme.focusBorder
            : (root.isHovered ? Theme.borderLight : "transparent")
        border.width: root.hasKeyboardFocus ? 2 : (root.isHovered ? 1 : 0)

        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
    }

    Rectangle {
        width: 4
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        radius: 2
        color: Theme.accentPrimary
        visible: root.hasKeyboardFocus
        opacity: visible ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }
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
        anchors.fill: parent
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
            width: 52
            height: 28
            radius: 14
            color: root.checked ? Theme.accentPrimary : Qt.rgba(1, 1, 1, 0.2)

            Behavior on color { ColorAnimation { duration: Theme.durationShort } }

            Rectangle {
                width: 22
                height: 22
                radius: 11
                x: root.checked ? parent.width - width - 3 : 3
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.textPrimary

                Behavior on x { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
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
