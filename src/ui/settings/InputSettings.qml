import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()
    readonly property Item preferredEntryItem: contextCombo
    property string selectedContext: "navigation"
    property string selectedDevice: "keyboard"
    property string capturingActionId: ""
    property string conflictMessage: ""

    readonly property var contexts: [
        { label: qsTr("Navigation"), value: "navigation" },
        { label: qsTr("Playback"), value: "playback" },
        { label: qsTr("Advanced Playback"), value: "advanced" }
    ]

    readonly property var devices: [
        { label: qsTr("Keyboard"), value: "keyboard" },
        { label: qsTr("Controller"), value: "gamepad" }
    ]

    readonly property var gamepadOptions: [
        { label: qsTr("None"), value: "" },
        { label: "A", value: "gamepad:a" },
        { label: "B", value: "gamepad:b" },
        { label: "X", value: "gamepad:x" },
        { label: "Y", value: "gamepad:y" },
        { label: qsTr("Start"), value: "gamepad:start" },
        { label: qsTr("Back/View"), value: "gamepad:back" },
        { label: qsTr("D-pad Up"), value: "gamepad:dpad_up" },
        { label: qsTr("D-pad Down"), value: "gamepad:dpad_down" },
        { label: qsTr("D-pad Left"), value: "gamepad:dpad_left" },
        { label: qsTr("D-pad Right"), value: "gamepad:dpad_right" },
        { label: qsTr("Left Stick Up"), value: "gamepad:left_stick_up" },
        { label: qsTr("Left Stick Down"), value: "gamepad:left_stick_down" },
        { label: qsTr("Left Stick Left"), value: "gamepad:left_stick_left" },
        { label: qsTr("Left Stick Right"), value: "gamepad:left_stick_right" },
        { label: qsTr("Right Stick Up"), value: "gamepad:right_stick_up" },
        { label: qsTr("Right Stick Down"), value: "gamepad:right_stick_down" },
        { label: qsTr("Left Bumper"), value: "gamepad:left_shoulder" },
        { label: qsTr("Right Bumper"), value: "gamepad:right_shoulder" },
        { label: qsTr("Left Trigger"), value: "gamepad:left_trigger" },
        { label: qsTr("Right Trigger"), value: "gamepad:right_trigger" },
        { label: qsTr("Left Stick Press"), value: "gamepad:left_stick_button" },
        { label: qsTr("Right Stick Press"), value: "gamepad:right_stick_button" }
    ]

    function enterFromRail() {
        contextCombo.forceActiveFocus()
    }

    function restoreFocus() {
        enterFromRail()
    }

    function actionsForContext() {
        var result = []
        var actions = InputBindingManager.actions
        for (var i = 0; i < actions.length; ++i) {
            if (actions[i].context === selectedContext) {
                result.push(actions[i])
            }
        }
        return result
    }

    function bindingText(actionId) {
        var bindings = InputBindingManager.bindingsForAction(selectedDevice, actionId)
        if (!bindings || bindings.length === 0) return qsTr("Unassigned")
        var labels = []
        for (var i = 0; i < bindings.length; ++i) {
            labels.push(InputBindingManager.displayTextForBinding(bindings[i]))
        }
        return labels.join(", ")
    }

    function optionIndexForValue(options, value) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].value === value) return i
        }
        return 0
    }

    function setSingleBinding(actionId, binding) {
        conflictMessage = ""
        if (!binding || binding.length === 0) {
            InputBindingManager.setBindingsForAction(selectedDevice, actionId, [])
            return
        }
        var conflicts = InputBindingManager.conflictsForBinding(selectedDevice, actionId, binding)
        if (conflicts && conflicts.length > 0) {
            conflictMessage = qsTr("Also assigned to %1").arg(conflicts[0].label)
        }
        InputBindingManager.setBindingsForAction(selectedDevice, actionId, [binding])
    }

    Keys.priority: Keys.AfterItem
    Keys.onLeftPressed: function(event) {
        if (capturingActionId.length === 0) {
            root.requestReturnToRail()
            event.accepted = true
        }
    }
    Keys.onEscapePressed: function(event) {
        if (capturingActionId.length > 0) {
            capturingActionId = ""
        } else {
            root.requestReturnToRail()
        }
        event.accepted = true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            SettingsComboBox {
                id: contextCombo
                model: root.contexts
                textRole: "label"
                valueRole: "value"
                Layout.preferredWidth: Math.round(260 * Theme.layoutScale)
                currentIndex: root.optionIndexForValue(root.contexts, root.selectedContext)
                onCurrentIndexChanged: {
                    if (currentIndex >= 0) root.selectedContext = root.contexts[currentIndex].value
                }
                Keys.onReturnPressed: function(event) { popup.open(); event.accepted = true }
                Keys.onEnterPressed: function(event) { popup.open(); event.accepted = true }
            }

            SettingsComboBox {
                id: deviceCombo
                model: root.devices
                textRole: "label"
                valueRole: "value"
                Layout.preferredWidth: Math.round(220 * Theme.layoutScale)
                currentIndex: root.optionIndexForValue(root.devices, root.selectedDevice)
                onCurrentIndexChanged: {
                    if (currentIndex >= 0) root.selectedDevice = root.devices[currentIndex].value
                }
                Keys.onReturnPressed: function(event) { popup.open(); event.accepted = true }
                Keys.onEnterPressed: function(event) { popup.open(); event.accepted = true }
            }

            Button {
                text: qsTr("Reset Section")
                focusPolicy: Qt.StrongFocus
                onClicked: InputBindingManager.resetContextBindings(root.selectedDevice, root.selectedContext)
            }

            Button {
                text: qsTr("Reset All")
                focusPolicy: Qt.StrongFocus
                onClicked: InputBindingManager.resetAllBindings()
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.conflictMessage
            visible: root.conflictMessage.length > 0
            color: Theme.errorColor
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
        }

        ListView {
            id: bindingList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSmall
            model: root.actionsForContext()

            delegate: Rectangle {
                id: row
                width: bindingList.width
                height: Math.round(86 * Theme.layoutScale)
                radius: Theme.radiusMedium
                color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.58)
                border.color: captureButton.activeFocus || gamepadCombo.activeFocus ? Theme.focusBorder : Theme.cardBorder
                border.width: captureButton.activeFocus || gamepadCombo.activeFocus ? 2 : 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Math.round(4 * Theme.layoutScale)
                        Text {
                            text: modelData.label
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: modelData.description
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Button {
                        id: captureButton
                        visible: root.selectedDevice === "keyboard"
                        Layout.preferredWidth: Math.round(280 * Theme.layoutScale)
                        text: root.capturingActionId === modelData.id ? qsTr("Press a key...") : root.bindingText(modelData.id)
                        focusPolicy: Qt.StrongFocus
                        onClicked: root.capturingActionId = modelData.id
                        Keys.onPressed: function(event) {
                            if (root.capturingActionId !== modelData.id) return
                            var binding = InputBindingManager.bindingForKeyboardEvent(event.key, event.modifiers)
                            root.setSingleBinding(modelData.id, binding)
                            root.capturingActionId = ""
                            event.accepted = true
                        }
                    }

                    SettingsComboBox {
                        id: gamepadCombo
                        visible: root.selectedDevice === "gamepad"
                        model: root.gamepadOptions
                        textRole: "label"
                        valueRole: "value"
                        Layout.preferredWidth: Math.round(280 * Theme.layoutScale)
                        currentIndex: {
                            var bindings = InputBindingManager.bindingsForAction("gamepad", modelData.id)
                            return root.optionIndexForValue(root.gamepadOptions, bindings && bindings.length > 0 ? bindings[0] : "")
                        }
                        onCurrentIndexChanged: {
                            if (currentIndex >= 0 && visible) {
                                root.setSingleBinding(modelData.id, root.gamepadOptions[currentIndex].value)
                            }
                        }
                        Keys.onReturnPressed: function(event) { popup.open(); event.accepted = true }
                        Keys.onEnterPressed: function(event) { popup.open(); event.accepted = true }
                    }

                    Button {
                        text: qsTr("Reset")
                        focusPolicy: Qt.StrongFocus
                        onClicked: InputBindingManager.resetActionBindings(root.selectedDevice, modelData.id)
                    }
                }
            }
        }
    }
}
