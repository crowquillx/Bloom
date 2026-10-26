import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    property alias input: textField
    property string label: ""
    property string description: ""
    property alias text: textField.text
    property alias placeholderText: textField.placeholderText
    property alias echoMode: textField.echoMode
    property Item keyUpTarget: null
    property Item keyDownTarget: null
    property var keyUpHandler: null
    property var keyDownHandler: null
    property var ensureVisible: null

    Accessible.role: Accessible.EditableText
    Accessible.name: label
    Accessible.description: description

    signal textEdited(string newText)
    signal editingFinished()

    Layout.fillWidth: true
    implicitHeight: frame.implicitHeight

    SettingsRow {
        id: frame
        anchors.left: parent.left
        anchors.right: parent.right
        label: root.label
        description: root.description
        highlighted: textField.activeFocus
        focusTarget: textField

        TextField {
            id: textField
            Layout.preferredWidth: Math.round(640 * Theme.layoutScale)
            Layout.maximumWidth: Math.round(frame.width * 0.55)
            Layout.alignment: Qt.AlignVCenter
            focus: true
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            cursorVisible: activeFocus

            color: Theme.textPrimary
            placeholderTextColor: Theme.textTertiary

            onActiveFocusChanged: {
                if (activeFocus && typeof InputModeManager !== "undefined") {
                    if (InputModeManager.pointerActive) {
                        InputModeManager.hideCursor(false)
                    } else {
                        InputModeManager.setNavigationMode("keyboard")
                        InputModeManager.hideCursor(true)
                    }
                }
                if (activeFocus && root.ensureVisible) root.ensureVisible(root)
            }

            leftPadding: Theme.spacingMedium
            rightPadding: Theme.spacingMedium
            // The row draws focus; the field only marks where typing goes.
            background: Rectangle {
                implicitHeight: Theme.buttonHeightSmall
                radius: Theme.radiusControl
                color: textField.activeFocus ? Theme.surfaceInteractivePressed : Theme.surfaceInteractive
                border.width: Theme.borderWidth
                border.color: textField.activeFocus ? Theme.surfaceBorderStrong : Theme.surfaceBorder
                opacity: textField.enabled ? 1.0 : Theme.disabledOpacity
            }

            Keys.onUpPressed: function(event) {
                if (typeof InputModeManager !== "undefined") {
                    InputModeManager.setNavigationMode("keyboard")
                    InputModeManager.hideCursor(true)
                }
                if (typeof root.keyUpHandler === "function") {
                    root.keyUpHandler(event)
                    if (event.accepted) return
                }
                if (root.keyUpTarget) {
                    root.keyUpTarget.forceActiveFocus()
                    event.accepted = true
                } else {
                    // Top of the page: let Up reach the navigation bar.
                    event.accepted = false
                }
            }

            Keys.onDownPressed: function(event) {
                if (typeof InputModeManager !== "undefined") {
                    InputModeManager.setNavigationMode("keyboard")
                    InputModeManager.hideCursor(true)
                }
                if (typeof root.keyDownHandler === "function") {
                    root.keyDownHandler(event)
                    if (event.accepted) return
                }
                if (root.keyDownTarget) {
                    root.keyDownTarget.forceActiveFocus()
                    event.accepted = true
                }
            }

            onTextEdited: root.textEdited(text)
            onEditingFinished: root.editingFinished()
        }
    }
}
