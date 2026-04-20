import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    property alias input: textField
    property string label: ""
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

    signal textEdited(string newText)
    signal editingFinished()

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight

    ColumnLayout {
        id: col
        width: parent.width
        spacing: Theme.spacingSmall

        Text {
            text: root.label
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textPrimary
            Layout.fillWidth: true
        }

        TextField {
            id: textField
            Layout.fillWidth: true
            focus: true
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            cursorVisible: activeFocus

            color: Theme.textPrimary
            placeholderTextColor: Theme.textSecondary

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

            background: Rectangle {
                implicitHeight: Theme.buttonHeightSmall
                radius: Theme.radiusSmall
                color: Theme.inputBackground
                border.color: textField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                border.width: textField.activeFocus ? 2 : Theme.borderWidth

                Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
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
