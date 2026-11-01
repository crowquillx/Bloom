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
    /// keyboard focus: screens offer the same action in a focusable place.
    property string actionText: ""

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
    height: Math.max(Theme.buttonHeightLarge, contentText.implicitHeight + Theme.spacingLarge * 2)

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
    Accessible.name: message

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

        Text {
            id: contentText
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, root.parent ? root.parent.width - Theme.paddingLarge * 2 - Theme.spacingXLarge * 3 : implicitWidth)
            text: root.message
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightMedium
            wrapMode: Text.WordWrap
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
        running: root.showing && root.autoHide
        onTriggered: root.hide()
    }

    function show(text) {
        showWithAction(text, "")
    }

    function showWithAction(text, action) {
        message = text
        actionText = action
        showing = true
        hideTimer.restart()
    }

    function hide() {
        if (!showing)
            return
        showing = false
        dismissed()
    }
}
