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
    property string capturingDevice: ""
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
        var revision = InputBindingManager.bindingsRevision
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
            conflictMessage = qsTr("Reassigned from %1").arg(conflicts[0].label)
        }
        InputBindingManager.setBindingForAction(selectedDevice, actionId, binding, true)
    }

    function beginCapture(actionId) {
        conflictMessage = ""
        capturingActionId = actionId
        capturingDevice = selectedDevice
        if (selectedDevice === "gamepad") {
            InputBindingManager.beginGamepadCapture(actionId)
            captureTimeout.restart()
        }
    }

    function cancelCapture() {
        if (capturingDevice === "gamepad") {
            InputBindingManager.cancelGamepadCapture()
        }
        capturingActionId = ""
        capturingDevice = ""
        captureTimeout.stop()
    }

    function focusFirstBinding() {
        if (bindingList.count <= 0) {
            return
        }
        bindingList.currentIndex = Math.max(0, bindingList.currentIndex)
        bindingList.forceActiveFocus()
        Qt.callLater(function() {
            var item = bindingList.itemAtIndex(bindingList.currentIndex)
            if (item && item.captureControl) {
                item.captureControl.forceActiveFocus()
            }
        })
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
            cancelCapture()
        } else {
            root.requestReturnToRail()
        }
        event.accepted = true
    }

    Timer {
        id: captureTimeout
        interval: 8000
        repeat: false
        onTriggered: root.cancelCapture()
    }

    Connections {
        target: InputBindingManager
        function onGamepadBindingCaptured(actionId, binding) {
            if (root.capturingActionId !== actionId || root.capturingDevice !== "gamepad") return
            root.setSingleBinding(actionId, binding)
            root.cancelCapture()
            Qt.callLater(function() { bindingList.forceActiveFocus() })
        }
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
                    if (currentIndex >= 0) {
                        root.cancelCapture()
                        root.selectedContext = root.contexts[currentIndex].value
                        Qt.callLater(function() { contextCombo.forceActiveFocus() })
                    }
                }
                Keys.onReturnPressed: function(event) { popup.open(); event.accepted = true }
                Keys.onEnterPressed: function(event) { popup.open(); event.accepted = true }
                Keys.onDownPressed: function(event) {
                    deviceCombo.forceActiveFocus()
                    event.accepted = true
                }
                KeyNavigation.tab: deviceCombo
                KeyNavigation.down: deviceCombo
                KeyNavigation.right: deviceCombo
            }

            SettingsComboBox {
                id: deviceCombo
                model: root.devices
                textRole: "label"
                valueRole: "value"
                Layout.preferredWidth: Math.round(220 * Theme.layoutScale)
                currentIndex: root.optionIndexForValue(root.devices, root.selectedDevice)
                onCurrentIndexChanged: {
                    if (currentIndex >= 0) {
                        root.cancelCapture()
                        root.selectedDevice = root.devices[currentIndex].value
                        Qt.callLater(function() { deviceCombo.forceActiveFocus() })
                    }
                }
                Keys.onReturnPressed: function(event) { popup.open(); event.accepted = true }
                Keys.onEnterPressed: function(event) { popup.open(); event.accepted = true }
                Keys.onDownPressed: function(event) {
                    root.focusFirstBinding()
                    event.accepted = true
                }
                KeyNavigation.tab: resetSectionButton
                KeyNavigation.down: bindingList
                KeyNavigation.left: contextCombo
                KeyNavigation.right: resetSectionButton
            }

            Button {
                id: resetSectionButton
                text: qsTr("Reset Section")
                focusPolicy: Qt.StrongFocus
                onClicked: {
                    root.cancelCapture()
                    InputBindingManager.resetDeviceBindings(root.selectedDevice)
                }
                Keys.onDownPressed: function(event) {
                    root.focusFirstBinding()
                    event.accepted = true
                }
                KeyNavigation.tab: resetAllButton
                KeyNavigation.down: bindingList
                KeyNavigation.left: deviceCombo
                KeyNavigation.right: resetAllButton
            }

            Button {
                id: resetAllButton
                text: qsTr("Reset All")
                focusPolicy: Qt.StrongFocus
                onClicked: {
                    root.cancelCapture()
                    InputBindingManager.resetAllBindings()
                }
                Keys.onDownPressed: function(event) {
                    root.focusFirstBinding()
                    event.accepted = true
                }
                KeyNavigation.tab: bindingList
                KeyNavigation.down: bindingList
                KeyNavigation.left: resetSectionButton
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
            activeFocusOnTab: true
            keyNavigationEnabled: true
            KeyNavigation.tab: contextCombo
            KeyNavigation.up: deviceCombo

            delegate: Rectangle {
                id: row
                property alias captureControl: captureButton
                width: bindingList.width
                height: Math.round(86 * Theme.layoutScale)
                radius: Theme.radiusMedium
                color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.58)
                border.color: captureButton.activeFocus ? Theme.focusBorder : Theme.cardBorder
                border.width: captureButton.activeFocus ? 2 : 1

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
                        Layout.preferredWidth: Math.round(280 * Theme.layoutScale)
                        text: root.capturingActionId === modelData.id
                              ? (root.selectedDevice === "keyboard" ? qsTr("Press a key...") : qsTr("Press controller input..."))
                              : root.bindingText(modelData.id)
                        focusPolicy: Qt.StrongFocus
                        onClicked: root.beginCapture(modelData.id)
                        KeyNavigation.tab: clearButton
                        KeyNavigation.right: clearButton
                        KeyNavigation.left: bindingList
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Escape) {
                                root.cancelCapture()
                                event.accepted = true
                                return
                            }
                            if (root.capturingActionId !== modelData.id) return
                            if (root.selectedDevice !== "keyboard") return
                            var binding = InputBindingManager.bindingForKeyboardEvent(event.key, event.modifiers)
                            root.setSingleBinding(modelData.id, binding)
                            root.cancelCapture()
                            event.accepted = true
                        }
                    }

                    Button {
                        id: clearButton
                        text: qsTr("Clear")
                        focusPolicy: Qt.StrongFocus
                        onClicked: {
                            root.cancelCapture()
                            root.setSingleBinding(modelData.id, "")
                        }
                        KeyNavigation.tab: resetButton
                        KeyNavigation.left: captureButton
                        KeyNavigation.right: resetButton
                    }

                    Button {
                        id: resetButton
                        text: qsTr("Reset")
                        focusPolicy: Qt.StrongFocus
                        onClicked: {
                            root.cancelCapture()
                            InputBindingManager.resetActionBindings(root.selectedDevice, modelData.id)
                            Qt.callLater(function() { captureButton.forceActiveFocus() })
                        }
                        KeyNavigation.tab: bindingList
                        KeyNavigation.left: clearButton
                    }
                }
            }
        }
    }
}
