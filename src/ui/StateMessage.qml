import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * StateMessage - shared loading / empty / error presentation.
 *
 * kind: "loading" | "empty" | "error"
 * Set actionText to show a focusable action (for example Retry); the action
 * emits `actionTriggered`. Call focusAction() to move keyboard focus to it.
 */
FocusScope {
    id: root

    property string kind: "empty"
    property string iconGlyph: kind === "error" ? Icons.error
                              : (kind === "empty" ? Icons.info : "")
    property string title: ""
    property string message: ""
    property string actionText: ""
    property bool compact: false
    // Surfaces that keep one palette whatever the theme (the playback
    // settings pop-out is always dark) set their own text colors.
    property color titleColor: Theme.textPrimary
    property color messageColor: Theme.textSecondary
    property color iconColor: kind === "error" ? Theme.errorColor : Theme.textTertiary

    signal actionTriggered()

    function focusAction() {
        if (actionButton.visible)
            actionButton.forceActiveFocus()
    }

    implicitWidth: Math.min(Math.round(760 * Theme.layoutScale), column.implicitWidth)
    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: title !== "" ? (title + (message !== "" ? ". " + message : "")) : message

    ColumnLayout {
        id: column
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width > 0 ? parent.width : implicitWidth, Math.round(760 * Theme.layoutScale))
        spacing: root.compact ? Theme.spacingSmall : Theme.spacingMedium

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: root.kind === "loading"
            running: visible
            Layout.preferredWidth: Math.round((root.compact ? 44 : 64) * Theme.layoutScale)
            Layout.preferredHeight: Layout.preferredWidth
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: root.kind !== "loading" && text !== ""
            text: root.iconGlyph
            font.family: Theme.fontIcon
            font.pixelSize: Math.round((root.compact ? 44 : 64) * Theme.layoutScale)
            color: root.iconColor
        }

        Text {
            Layout.fillWidth: true
            visible: text !== ""
            text: root.title
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: root.compact ? Theme.fontSizeBody : Theme.fontSizeTitle
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightStrong
            color: root.titleColor
        }

        Text {
            Layout.fillWidth: true
            visible: text !== ""
            text: root.message
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: root.compact ? Theme.fontSizeSmall : Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: root.messageColor
        }

        ActionButton {
            id: actionButton
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingSmall
            visible: root.actionText !== "" && root.kind !== "loading"
            text: root.actionText
            iconGlyph: root.kind === "error" ? Icons.refresh : ""
            focus: true
            onClicked: root.actionTriggered()
            Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
            Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
        }
    }
}
