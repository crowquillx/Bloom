import QtQuick
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()
    readonly property bool scrolled: flickable.scrolled

    readonly property Item preferredEntryItem: framerateToggle
    property Item _lastFocusedItem: null
    readonly property var hdrOutputModeValues: ["match-content", "tone-map-to-sdr", "force-hdr-experimental"]
    readonly property var hdrOutputModeLabels: [qsTr("Match Content"), qsTr("Tone-map to SDR"), qsTr("Force HDR (Experimental)")]
    readonly property var dolbyVisionFallbackModeValues: ["prefer-compatible-hdr", "tone-map-unsupported", "experimental-direct-play"]
    readonly property var dolbyVisionFallbackModeLabels: [qsTr("Prefer Compatible HDR"), qsTr("Tone-map Unsupported"), qsTr("Experimental Direct Play")]
    readonly property bool linuxDisplayCommandsVisible: Qt.platform.os === "linux"
                                                         && !SystemPowerController.isFlatpak

    function enterFromRail() {
        var target = (_lastFocusedItem && _lastFocusedItem.visible) ? _lastFocusedItem : preferredEntryItem
        if (target) target.forceActiveFocus()
    }

    function restoreFocus() {
        enterFromRail()
    }

    function indexForValue(values, value) {
        var index = values.indexOf(value)
        return index >= 0 ? index : 0
    }

    Keys.priority: Keys.AfterItem
    Keys.onLeftPressed: function(event) { requestReturnToRail(); event.accepted = true }
    Keys.onEscapePressed: function(event) { requestReturnToRail(); event.accepted = true }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        SettingsScrollView {
            id: flickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: contentColumn.implicitHeight + Theme.pageMarginBottom

            ColumnLayout {
                id: contentColumn
                x: Theme.spacingMedium
                y: Theme.spacingSmall
                width: flickable.width - 2 * Theme.spacingMedium
                spacing: Theme.spacingSmall

                SectionHeader {
                    level: "group"
                    text: qsTr("Refresh Rate")
                    Layout.fillWidth: true
                    Layout.topMargin: 0
                }

                SettingsToggleRow {
                    id: framerateToggle
                    label: qsTr("Enable Framerate Matching")
                    description: qsTr("Automatically adjust display refresh rate to match video content")
                    checked: ConfigManager.enableFramerateMatching
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onToggled: function(value) { ConfigManager.enableFramerateMatching = value }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.down: skipCompatibleToggle.enabled ? skipCompatibleToggle : refreshDelaySlider.enabled ? refreshDelaySlider : hdrToggle
                }

                SettingsToggleRow {
                    id: skipCompatibleToggle
                    label: qsTr("Skip switch when refresh rate is already a multiple")
                    description: qsTr("Avoid changing refresh rate when the display is already running at a compatible multiple (e.g. 120Hz for 24fps content)")
                    checked: ConfigManager.skipRefreshRateOnCompatibleMultiple
                    enabled: ConfigManager.enableFramerateMatching
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onToggled: function(value) { ConfigManager.skipRefreshRateOnCompatibleMultiple = value }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: framerateToggle
                    KeyNavigation.down: refreshDelaySlider.enabled ? refreshDelaySlider : hdrToggle
                }

                SettingsSliderRow {
                    id: refreshDelaySlider
                    label: qsTr("Refresh Rate Switch Delay")
                    description: qsTr("Seconds to wait after switching refresh rate before starting playback (prevents dropped frames)")
                    value: ConfigManager.framerateMatchDelay
                    from: 0
                    to: 5
                    stepSize: 1
                    unit: "s"
                    enabled: ConfigManager.enableFramerateMatching
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onSliderValueChanged: function(newValue) { ConfigManager.framerateMatchDelay = newValue }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: skipCompatibleToggle.enabled ? skipCompatibleToggle : framerateToggle
                    KeyNavigation.down: hdrToggle
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("HDR")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsToggleRow {
                    id: hdrToggle
                    label: qsTr("Enable HDR")
                    description: qsTr("Enable High Dynamic Range for compatible displays")
                    checked: ConfigManager.enableHDR
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onToggled: function(value) { ConfigManager.enableHDR = value }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: refreshDelaySlider.enabled ? refreshDelaySlider : skipCompatibleToggle.enabled ? skipCompatibleToggle : framerateToggle
                    KeyNavigation.down: advancedToggle
                }

                SettingsRow {
                    id: advancedToggle
                    property bool advancedExpanded: false
                    Layout.topMargin: Theme.spacingLarge
                    label: qsTr("Advanced")
                    description: qsTr("HDR output, Dolby Vision fallback and display commands")
                    Accessible.role: Accessible.Button
                    Accessible.name: advancedExpanded ? qsTr("Collapse Advanced") : qsTr("Expand Advanced")

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root._lastFocusedItem = this
                            flickable.ensureFocusVisible(this)
                        }
                    }

                    KeyNavigation.up: hdrToggle
                    KeyNavigation.down: advancedExpanded ? hdrOutputModeCombo : null

                    onActivated: advancedExpanded = !advancedExpanded

                    Text {
                        text: advancedToggle.advancedExpanded ? Icons.expandLess : Icons.expandMore
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.iconSizeLarge
                        color: advancedToggle.highlighted ? Theme.textPrimary : Theme.textSecondary
                        Accessible.ignored: true
                    }
                }

                // --- Advanced Controls (collapsible) ---
                ColumnLayout {
                    id: advancedContent
                    visible: advancedToggle.advancedExpanded
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    SettingsRow {
                        id: hdrOutputModeRow
                        label: qsTr("HDR Output Mode")
                        description: qsTr("Match content keeps SDR in SDR and HDR in HDR when supported")
                        dimmed: !hdrOutputModeCombo.enabled
                        highlighted: hdrOutputModeCombo.activeFocus || hdrOutputModeCombo.popup.visible
                        focusTarget: hdrOutputModeCombo
                        onActivated: if (hdrOutputModeCombo.enabled) hdrOutputModeCombo.popup.open()

                        SettingsComboBox {
                            id: hdrOutputModeCombo
                            model: root.hdrOutputModeLabels
                            currentIndex: root.indexForValue(root.hdrOutputModeValues, ConfigManager.hdrOutputMode)

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root._lastFocusedItem = this
                                    flickable.ensureFocusVisible(this)
                                }
                            }

                            onActivated: function(index) {
                                ConfigManager.hdrOutputMode = root.hdrOutputModeValues[index]
                            }
                            // The shared popup reports keyboard and pointer picks here.
                            onSelectionAccepted: function(index) {
                                ConfigManager.hdrOutputMode = root.hdrOutputModeValues[index]
                            }

                            Keys.onUpPressed: function(event) {
                                if (!popup.visible) {
                                    advancedToggle.forceActiveFocus()
                                    event.accepted = true
                                }
                            }
                            Keys.onDownPressed: function(event) {
                                if (!popup.visible) {
                                    dolbyVisionFallbackCombo.forceActiveFocus()
                                    event.accepted = true
                                }
                            }
                            Keys.onReturnPressed: popup.open()
                            Keys.onEnterPressed: popup.open()
                        }
                    }

                    SettingsRow {
                        id: dolbyVisionFallbackRow
                        label: qsTr("Dolby Vision Fallback")
                        description: qsTr("Unsupported Dolby Vision is locally tone-mapped instead of transcoded")
                        dimmed: !dolbyVisionFallbackCombo.enabled
                        highlighted: dolbyVisionFallbackCombo.activeFocus || dolbyVisionFallbackCombo.popup.visible
                        focusTarget: dolbyVisionFallbackCombo
                        onActivated: if (dolbyVisionFallbackCombo.enabled) dolbyVisionFallbackCombo.popup.open()

                        SettingsComboBox {
                            id: dolbyVisionFallbackCombo
                            model: root.dolbyVisionFallbackModeLabels
                            currentIndex: root.indexForValue(root.dolbyVisionFallbackModeValues, ConfigManager.dolbyVisionFallbackMode)

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root._lastFocusedItem = this
                                    flickable.ensureFocusVisible(this)
                                }
                            }

                            onActivated: function(index) {
                                ConfigManager.dolbyVisionFallbackMode = root.dolbyVisionFallbackModeValues[index]
                            }
                            onSelectionAccepted: function(index) {
                                ConfigManager.dolbyVisionFallbackMode = root.dolbyVisionFallbackModeValues[index]
                            }

                            Keys.onUpPressed: function(event) {
                                if (!popup.visible) {
                                    hdrOutputModeCombo.forceActiveFocus()
                                    event.accepted = true
                                }
                            }
                            Keys.onDownPressed: function(event) {
                                if (!popup.visible) {
                                    var target = root.linuxDisplayCommandsVisible
                                                  ? linuxRefreshRateInput.input
                                                  : windowsHDRInput.input
                                    target.forceActiveFocus()
                                    event.accepted = true
                                }
                            }
                            Keys.onReturnPressed: popup.open()
                            Keys.onEnterPressed: popup.open()
                        }
                    }

                    SettingsTextInputRow {
                        id: linuxRefreshRateInput
                        visible: root.linuxDisplayCommandsVisible
                        label: qsTr("Linux Refresh Rate Command")
                        placeholderText: "xrandr --output HDMI-1 --rate {RATE}"
                        text: ConfigManager.linuxRefreshRateCommand
                        ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                        keyUpTarget: dolbyVisionFallbackCombo
                        keyDownTarget: linuxHDRInput.input
                        onEditingFinished: ConfigManager.linuxRefreshRateCommand = text
                        onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    }

                    SettingsTextInputRow {
                        id: linuxHDRInput
                        visible: root.linuxDisplayCommandsVisible
                        label: qsTr("Linux HDR Command")
                        placeholderText: qsTr("(not commonly available)")
                        text: ConfigManager.linuxHDRCommand
                        ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                        keyUpTarget: linuxRefreshRateInput.input
                        keyDownTarget: windowsHDRInput.input
                        onEditingFinished: ConfigManager.linuxHDRCommand = text
                        onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    }

                    SettingsTextInputRow {
                        id: windowsHDRInput
                        label: qsTr("Windows Custom HDR Command")
                        placeholderText: qsTr("(uses native API by default)")
                        text: ConfigManager.windowsCustomHDRCommand
                        ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                        keyUpHandler: function(event) {
                            if (root.linuxDisplayCommandsVisible) {
                                linuxHDRInput.input.forceActiveFocus()
                            } else {
                                dolbyVisionFallbackCombo.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                        keyDownTarget: null
                        onEditingFinished: ConfigManager.windowsCustomHDRCommand = text
                        onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    }

                    Text {
                        text: qsTr("Use {RATE} for exact refresh rate (e.g., 23.976) or {RATE_INT} for rounded integer (e.g., 24). Use {STATE} for on/off in HDR commands.")
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingSmall
                    }
                }
            }
        }
    }
}
