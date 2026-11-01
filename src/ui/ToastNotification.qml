import QtQuick
import QtQuick.Controls

import BloomUI

/**
 * Toast Notification Component
 *
 * Displays a brief notification message that auto-hides after a timeout.
 * Used for non-blocking informational messages to the user.
 */
Rectangle {
    id: root

    // Configuration
    property string message: ""
    property int displayDuration: 5000  // ms
    property bool autoHide: true
    /// Optional action shown after the message (Undo). It takes no
    /// keyboard focus: screens offer the same action in a focusable place
    /// and say how to reach it in `hint`.
    property string actionText: ""
    /// Optional second line, such as the key that reaches the action.
    property string hint: ""
    /// Holds the timeout while the action can be reached elsewhere (a menu
    /// offering it is open); the full duration starts again afterwards.
    property bool paused: false

    signal actionTriggered()
    /// The toast went away (timeout, hide(), or its action).
    signal dismissed()

    // Internal state
    property bool showing: false

    // Positioning
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Math.round(72 * Theme.layoutScale)

    // Size based on content
    width: Math.min(contentRow.implicitWidth + Theme.spacingXLarge * 2, parent.width - Theme.paddingLarge * 2)
    height: Math.max(Theme.buttonHeightLarge, textColumn.implicitHeight + Theme.spacingLarge * 2)

    // Appearance
    radius: Theme.radiusLarge
    color: Theme.surfaceDialog
    border.width: Theme.borderWidth
    border.color: Theme.surfaceBorderStrong
    opacity: showing ? 1.0 : 0.0
    visible: opacity > 0
    transform: Translate { y: root.showing ? 0 : Math.round(16 * Theme.layoutScale) }

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.durationShort
            easing.type: Easing.InOutQuad
        }
    }

    Accessible.role: Accessible.AlertMessage
    Accessible.name: hint !== "" ? message + ". " + hint : message

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: Theme.spacingMedium

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.selectionIndicatorWidth
            height: Math.round(28 * Theme.layoutScale)
            radius: width / 2
            color: Theme.accentPrimary
        }

        Column {
            id: textColumn
            anchors.verticalCenter: parent.verticalCenter
            readonly property real maxTextWidth: root.parent
                ? root.parent.width - Theme.paddingLarge * 2 - Theme.spacingXLarge * 3
                : Number.MAX_VALUE
            spacing: Theme.spacingXSmall

            Text {
                id: contentText
                width: Math.min(implicitWidth, textColumn.maxTextWidth)
                text: root.message
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightMedium
                wrapMode: Text.WordWrap
            }

            Text {
                width: Math.min(implicitWidth, textColumn.maxTextWidth)
                visible: root.hint !== ""
                text: root.hint
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                wrapMode: Text.WordWrap
            }
        }

        ActionButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.actionText !== ""
            variant: "ghost"
            text: root.actionText
            focusPolicy: Qt.NoFocus
            implicitHeight: Theme.buttonHeightSmall
            onClicked: {
                root.actionTriggered()
                root.hide()
            }
        }
    }

    Timer {
        id: hideTimer
        interval: root.displayDuration
        running: root.showing && root.autoHide && !root.paused
        onTriggered: root.hide()
    }

    function show(text) {
        showWithAction(text, "", "")
    }

    function showWithAction(text, action, actionHint) {
        message = text
        actionText = action
        hint = actionHint || ""
        showing = true
        if (!paused)
            hideTimer.restart()
    }

    function hide() {
        if (!showing)
            return
        showing = false
        dismissed()
    }
}
