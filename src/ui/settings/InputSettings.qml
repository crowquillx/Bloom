import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()

    readonly property Item preferredEntryItem: contextCombo
    property Item _lastFocusedItem: null
    property string _lastFocusedActionId: ""
    property string _lastFocusedControl: "capture"
    property string selectedContext: "navigation"
    property string selectedDevice: "keyboard"
    property string capturingActionId: ""
    property string capturingDevice: ""
    property string conflictMessage: ""
    readonly property var currentActions: actionsForContext()

    onVisibleChanged: {
        if (!visible && capturingActionId !== "") cancelCapture()
    }

    readonly property var contexts: [
        { label: qsTr("Navigation"), value: "navigation" },
        { label: qsTr("Playback"), value: "playback" },
        { label: qsTr("Advanced Playback"), value: "advanced" }
    ]

    readonly property var devices: [
        { label: qsTr("Keyboard"), value: "keyboard" },
        { label: qsTr("Controller"), value: "gamepad" }
    ]

    component SettingsActionButton: Button {
        id: actionButton

        property bool primary: false
        property bool destructive: false
        property var ensureVisible: null

        focusPolicy: Qt.StrongFocus
        implicitHeight: Theme.buttonHeightSmall
        leftPadding: Theme.spacingMedium
        rightPadding: Theme.spacingMedium

        Accessible.role: Accessible.Button
        Accessible.name: text

        onActiveFocusChanged: {
            if (activeFocus && ensureVisible) ensureVisible(actionButton)
        }

        contentItem: Text {
            text: actionButton.text
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            font.weight: actionButton.primary ? Font.DemiBold : Font.Normal
            color: actionButton.enabled ? Theme.textPrimary : Theme.textDisabled
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            implicitHeight: Theme.buttonHeightSmall
            radius: Theme.radiusSmall
            color: {
                if (!actionButton.enabled) return Theme.buttonSecondaryBackgroundDisabled
                if (actionButton.down) {
                    return actionButton.primary ? Theme.buttonPrimaryBackgroundPressed : Theme.buttonSecondaryBackgroundPressed
                }
                if (actionButton.hovered || actionButton.activeFocus) {
                    if (actionButton.destructive) return Theme.destructiveHoverBg
                    return actionButton.primary ? Theme.buttonPrimaryBackgroundHover : Theme.buttonSecondaryBackgroundHover
                }
                if (actionButton.destructive) return "transparent"
                return actionButton.primary ? Theme.buttonPrimaryBackground : Theme.buttonSecondaryBackground
            }
            border.color: {
                if (actionButton.activeFocus) return Theme.focusBorder
                if (actionButton.destructive) return Theme.destructiveBorder
                if (actionButton.hovered) return actionButton.primary ? Theme.buttonPrimaryBorderFocused : Theme.buttonSecondaryBorderHover
                return actionButton.primary ? Theme.buttonPrimaryBorder : Theme.buttonSecondaryBorder
            }
            border.width: actionButton.activeFocus ? 2 : Theme.buttonBorderWidth
            opacity: actionButton.enabled ? 1.0 : 0.55

            Behavior on color { ColorAnimation { duration: Theme.durationShort } }
            Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
        }
    }

    function enterFromRail() {
        if (_lastFocusedItem && _lastFocusedItem.visible && _lastFocusedItem.enabled) {
            _lastFocusedItem.forceActiveFocus()
            return
        }
        if (_lastFocusedActionId.length > 0 && focusBindingControl(_lastFocusedActionId, _lastFocusedControl)) {
            return
        }
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
        rememberBindingFocus(actionId, "capture")
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

    function isModifierKey(key) {
        return key === Qt.Key_Shift
            || key === Qt.Key_Control
            || key === Qt.Key_Alt
            || key === Qt.Key_Meta
            || key === Qt.Key_AltGr
    }

    function rememberPanelFocus(item) {
        _lastFocusedItem = item
        _lastFocusedActionId = ""
        _lastFocusedControl = ""
        flickable.ensureFocusVisible(item)
        InputModeManager.setNavigationMode("keyboard")
        InputModeManager.hideCursor(true)
    }

    function rememberBindingFocus(actionId, controlName) {
        _lastFocusedActionId = actionId
        _lastFocusedControl = controlName
    }

    function rowAt(index) {
        if (index < 0 || index >= bindingRepeater.count) return null
        return bindingRepeater.itemAt(index)
    }

    function focusBindingControl(actionId, controlName) {
        for (var i = 0; i < bindingRepeater.count; ++i) {
            var row = bindingRepeater.itemAt(i)
            if (row && row.actionId === actionId) {
                row.focusControl(controlName)
                return true
            }
        }
        return false
    }

    function focusFirstBinding() {
        var row = rowAt(0)
        if (row) row.focusControl("capture")
    }

    function refocusAction(actionId, controlName) {
        Qt.callLater(function() {
            if (!root.focusBindingControl(actionId, controlName || "capture")) {
                root.enterFromRail()
            }
        })
    }

    function focusAfterTopAction(item) {
        Qt.callLater(function() {
            if (item && item.visible && item.enabled) {
                item.forceActiveFocus()
            } else {
                root.enterFromRail()
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
            refocusAction(_lastFocusedActionId, _lastFocusedControl)
        } else {
            root.requestReturnToRail()
        }
        event.accepted = true
    }

    Timer {
        id: captureTimeout
        interval: 8000
        repeat: false
        onTriggered: {
            var actionId = root.capturingActionId
            root.cancelCapture()
            root.refocusAction(actionId, "capture")
        }
    }

    Connections {
        enabled: root.visible
        target: InputBindingManager
        function onGamepadBindingCaptured(actionId, binding) {
            if (root.capturingActionId !== actionId || root.capturingDevice !== "gamepad") return
            root.setSingleBinding(actionId, binding)
            root.cancelCapture()
            root.refocusAction(actionId, "capture")
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.76)
        border.color: Theme.cardBorder
        border.width: Theme.borderWidth

        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.08) }
            GradientStop { position: 0.3; color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.78) }
            GradientStop { position: 1.0; color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.66) }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: 0

        Text {
            text: qsTr("Input")
            font.pixelSize: Theme.fontSizeTitle
            font.family: Theme.fontPrimary
            font.weight: Font.DemiBold
            color: Theme.textPrimary
            Layout.bottomMargin: Theme.spacingMedium
        }

        Flickable {
            id: flickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: contentColumn.implicitHeight + 2 * Theme.spacingSmall
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            function ensureFocusVisible(item) {
                if (!item) return
                var mapped = item.mapToItem(contentColumn, 0, 0)
                var itemY = contentColumn.y + mapped.y
                var itemHeight = item.height
                var viewTop = contentY
                var viewBottom = contentY + height
                var padding = 50
                var maxScroll = Math.max(0, contentHeight - height)

                if (itemY < viewTop + padding) {
                    contentY = Math.max(0, itemY - padding)
                } else if (itemY + itemHeight > viewBottom - padding) {
                    contentY = Math.min(maxScroll, itemY + itemHeight - height + padding)
                }
            }

            ColumnLayout {
                id: contentColumn
                x: Theme.spacingSmall
                y: Theme.spacingSmall
                width: flickable.width - 2 * Theme.spacingSmall
                spacing: Theme.spacingMedium

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Math.round(4 * Theme.layoutScale)

                        Text {
                            text: qsTr("Context")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        SettingsComboBox {
                            id: contextCombo
                            model: root.contexts
                            textRole: "label"
                            valueRole: "value"
                            Layout.preferredWidth: Math.round(280 * Theme.layoutScale)
                            currentIndex: root.optionIndexForValue(root.contexts, root.selectedContext)

                            onActiveFocusChanged: {
                                if (activeFocus) root.rememberPanelFocus(this)
                            }
                            onCurrentIndexChanged: {
                                if (currentIndex >= 0 && root.contexts[currentIndex].value !== root.selectedContext) {
                                    root.cancelCapture()
                                    root.selectedContext = root.contexts[currentIndex].value
                                    root._lastFocusedItem = contextCombo
                                    Qt.callLater(function() { contextCombo.forceActiveFocus() })
                                }
                            }
                            KeyNavigation.down: deviceCombo
                            KeyNavigation.right: deviceCombo
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Math.round(4 * Theme.layoutScale)

                        Text {
                            text: qsTr("Device")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        SettingsComboBox {
                            id: deviceCombo
                            model: root.devices
                            textRole: "label"
                            valueRole: "value"
                            Layout.preferredWidth: Math.round(240 * Theme.layoutScale)
                            currentIndex: root.optionIndexForValue(root.devices, root.selectedDevice)

                            onActiveFocusChanged: {
                                if (activeFocus) root.rememberPanelFocus(this)
                            }
                            onCurrentIndexChanged: {
                                if (currentIndex >= 0 && root.devices[currentIndex].value !== root.selectedDevice) {
                                    root.cancelCapture()
                                    root.selectedDevice = root.devices[currentIndex].value
                                    root._lastFocusedItem = deviceCombo
                                    Qt.callLater(function() { deviceCombo.forceActiveFocus() })
                                }
                            }
                            Keys.onDownPressed: function(event) {
                                root.focusFirstBinding()
                                event.accepted = true
                            }
                            KeyNavigation.up: contextCombo
                            KeyNavigation.down: resetSectionButton
                            KeyNavigation.left: contextCombo
                            KeyNavigation.right: resetSectionButton
                        }
                    }

                    SettingsActionButton {
                        id: resetSectionButton
                        text: qsTr("Reset Section")
                        Layout.alignment: Qt.AlignBottom
                        ensureVisible: function(item) { root.rememberPanelFocus(item) }
                        onClicked: {
                            root.cancelCapture()
                            InputBindingManager.resetContextBindings(root.selectedDevice, root.selectedContext)
                            root.focusAfterTopAction(resetSectionButton)
                        }
                        Keys.onDownPressed: function(event) {
                            root.focusFirstBinding()
                            event.accepted = true
                        }
                        KeyNavigation.left: deviceCombo
                        KeyNavigation.right: resetAllButton
                        KeyNavigation.down: resetAllButton
                    }

                    SettingsActionButton {
                        id: resetAllButton
                        text: qsTr("Reset All")
                        destructive: true
                        Layout.alignment: Qt.AlignBottom
                        ensureVisible: function(item) { root.rememberPanelFocus(item) }
                        onClicked: {
                            root.cancelCapture()
                            InputBindingManager.resetAllBindings()
                            root.focusAfterTopAction(resetAllButton)
                        }
                        Keys.onDownPressed: function(event) {
                            root.focusFirstBinding()
                            event.accepted = true
                        }
                        KeyNavigation.left: resetSectionButton
                        KeyNavigation.down: root.rowAt(0) ? root.rowAt(0).captureControl : null
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.conflictMessage
                    visible: root.conflictMessage.length > 0
                    color: Theme.errorColor
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    wrapMode: Text.WordWrap
                }

                SettingsGroupDivider { Layout.fillWidth: true }

                Repeater {
                    id: bindingRepeater
                    model: root.currentActions

                    delegate: FocusScope {
                        id: row

                        required property int index
                        required property var modelData
                        readonly property string actionId: modelData.id
                        readonly property alias captureControl: captureButton

                        Layout.fillWidth: true
                        implicitHeight: Math.max(rowContent.implicitHeight + 2 * Theme.spacingMedium,
                                                 Math.round(92 * Theme.layoutScale))

                        function focusControl(controlName) {
                            if (controlName === "clear") {
                                clearButton.forceActiveFocus()
                            } else if (controlName === "reset") {
                                resetButton.forceActiveFocus()
                            } else {
                                captureButton.forceActiveFocus()
                            }
                            flickable.ensureFocusVisible(row)
                        }

                        function previousRow() {
                            return root.rowAt(index - 1)
                        }

                        function nextRow() {
                            return root.rowAt(index + 1)
                        }

                        function focusPreviousRowOrHeader() {
                            var previous = previousRow()
                            if (previous) previous.focusControl("capture")
                            else deviceCombo.forceActiveFocus()
                        }

                        function focusNextRow() {
                            var next = nextRow()
                            if (next) next.focusControl("capture")
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusMedium
                            color: row.activeFocus
                                   ? Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.12)
                                   : Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.48)
                            border.color: row.activeFocus ? Theme.focusBorder : Theme.cardBorder
                            border.width: row.activeFocus ? 2 : Theme.borderWidth

                            Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                            Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                        }

                        Rectangle {
                            width: 4
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.margins: Theme.spacingSmall
                            radius: 2
                            color: Theme.accentPrimary
                            opacity: row.activeFocus ? 1.0 : 0.0

                            Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }
                        }

                        RowLayout {
                            id: rowContent
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMedium
                            spacing: Theme.spacingMedium

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Math.round(4 * Theme.layoutScale)

                                Text {
                                    text: row.modelData.label
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: row.modelData.description
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }

                            SettingsActionButton {
                                id: captureButton
                                primary: root.capturingActionId === row.actionId
                                Layout.preferredWidth: Math.round(320 * Theme.layoutScale)
                                text: root.capturingActionId === row.actionId
                                      ? (root.selectedDevice === "keyboard" ? qsTr("Press a key...") : qsTr("Press controller input..."))
                                      : root.bindingText(row.actionId)
                                ensureVisible: function(item) {
                                    root._lastFocusedItem = item
                                    root.rememberBindingFocus(row.actionId, "capture")
                                    flickable.ensureFocusVisible(row)
                                    InputModeManager.setNavigationMode("keyboard")
                                    InputModeManager.hideCursor(true)
                                }
                                onClicked: root.beginCapture(row.actionId)
                                KeyNavigation.right: clearButton
                                KeyNavigation.up: row.previousRow() ? row.previousRow().captureControl : deviceCombo
                                KeyNavigation.down: row.nextRow() ? row.nextRow().captureControl : null
                                Keys.onUpPressed: function(event) {
                                    if (root.capturingActionId === row.actionId) return
                                    row.focusPreviousRowOrHeader()
                                    event.accepted = true
                                }
                                Keys.onDownPressed: function(event) {
                                    if (root.capturingActionId === row.actionId) return
                                    row.focusNextRow()
                                    event.accepted = true
                                }
                                Keys.onPressed: function(event) {
                                    if (event.key === Qt.Key_Escape && root.capturingActionId === row.actionId) {
                                        root.cancelCapture()
                                        root.refocusAction(row.actionId, "capture")
                                        event.accepted = true
                                        return
                                    }
                                    if (root.capturingActionId !== row.actionId) return
                                    if (root.selectedDevice !== "keyboard") return
                                    if (root.isModifierKey(event.key)) {
                                        event.accepted = true
                                        return
                                    }
                                    var binding = InputBindingManager.bindingForKeyboardEvent(event.key, event.modifiers)
                                    root.setSingleBinding(row.actionId, binding)
                                    root.cancelCapture()
                                    root.refocusAction(row.actionId, "capture")
                                    event.accepted = true
                                }
                            }

                            SettingsActionButton {
                                id: clearButton
                                text: qsTr("Clear")
                                Layout.preferredWidth: Math.round(110 * Theme.layoutScale)
                                ensureVisible: function(item) {
                                    root._lastFocusedItem = item
                                    root.rememberBindingFocus(row.actionId, "clear")
                                    flickable.ensureFocusVisible(row)
                                    InputModeManager.setNavigationMode("keyboard")
                                    InputModeManager.hideCursor(true)
                                }
                                onClicked: {
                                    root.cancelCapture()
                                    root.setSingleBinding(row.actionId, "")
                                    root.refocusAction(row.actionId, "clear")
                                }
                                KeyNavigation.left: captureButton
                                KeyNavigation.right: resetButton
                                KeyNavigation.up: row.previousRow() ? row.previousRow().captureControl : deviceCombo
                                KeyNavigation.down: row.nextRow() ? row.nextRow().captureControl : null
                                Keys.onUpPressed: function(event) {
                                    row.focusPreviousRowOrHeader()
                                    event.accepted = true
                                }
                                Keys.onDownPressed: function(event) {
                                    row.focusNextRow()
                                    event.accepted = true
                                }
                            }

                            SettingsActionButton {
                                id: resetButton
                                text: qsTr("Reset")
                                Layout.preferredWidth: Math.round(110 * Theme.layoutScale)
                                ensureVisible: function(item) {
                                    root._lastFocusedItem = item
                                    root.rememberBindingFocus(row.actionId, "reset")
                                    flickable.ensureFocusVisible(row)
                                    InputModeManager.setNavigationMode("keyboard")
                                    InputModeManager.hideCursor(true)
                                }
                                onClicked: {
                                    root.cancelCapture()
                                    InputBindingManager.resetActionBindings(root.selectedDevice, row.actionId)
                                    root.refocusAction(row.actionId, "reset")
                                }
                                KeyNavigation.left: clearButton
                                KeyNavigation.up: row.previousRow() ? row.previousRow().captureControl : deviceCombo
                                KeyNavigation.down: row.nextRow() ? row.nextRow().captureControl : null
                                Keys.onUpPressed: function(event) {
                                    row.focusPreviousRowOrHeader()
                                    event.accepted = true
                                }
                                Keys.onDownPressed: function(event) {
                                    row.focusNextRow()
                                    event.accepted = true
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
