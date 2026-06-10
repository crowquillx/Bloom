import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()

    readonly property Item preferredEntryItem: framerateToggle
    property Item _lastFocusedItem: null
    readonly property var hdrOutputModeValues: ["match-content", "tone-map-to-sdr", "force-hdr-experimental"]
    readonly property var hdrOutputModeLabels: [qsTr("Match Content"), qsTr("Tone-map to SDR"), qsTr("Force HDR (Experimental)")]
    readonly property var dolbyVisionFallbackModeValues: ["prefer-compatible-hdr", "tone-map-unsupported", "experimental-direct-play"]
    readonly property var dolbyVisionFallbackModeLabels: [qsTr("Prefer Compatible HDR"), qsTr("Tone-map Unsupported"), qsTr("Experimental Direct Play")]

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

    // Glass card background
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
            text: qsTr("Video")
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

                // --- Enable Framerate Matching ---
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

                // --- Skip Compatible Multiple ---
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

                // --- Refresh Rate Switch Delay ---
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

                SettingsGroupDivider { Layout.fillWidth: true }

                // --- Enable HDR ---
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

                SettingsGroupDivider { Layout.fillWidth: true }

                // --- Advanced Section ---
                Button {
                    id: advancedToggle
                    property bool advancedExpanded: false
                    Layout.alignment: Qt.AlignLeft
                    Layout.topMargin: Theme.spacingSmall
                    focusPolicy: Qt.StrongFocus

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root._lastFocusedItem = this
                            flickable.ensureFocusVisible(this)
                        }
                    }

                    KeyNavigation.up: hdrToggle
                    KeyNavigation.down: advancedExpanded ? hdrOutputModeCombo : null

                    contentItem: RowLayout {
                        spacing: Theme.spacingSmall
                        Text {
                            text: advancedToggle.advancedExpanded ? "▼" : "▶"
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeSmall
                            color: advancedToggle.activeFocus ? Theme.textPrimary : Theme.textSecondary
                        }
                        Text {
                            text: qsTr("Advanced")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: advancedToggle.activeFocus ? Theme.textPrimary : Theme.textSecondary
                        }
                    }

                    background: Rectangle {
                        implicitWidth: 140
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: advancedToggle.activeFocus ? Theme.buttonSecondaryBackgroundHover : (advancedToggle.hovered ? Theme.buttonSecondaryBackgroundHover : "transparent")
                        border.color: advancedToggle.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
                        border.width: advancedToggle.activeFocus ? 2 : Theme.buttonBorderWidth

                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    }

                    onClicked: advancedExpanded = !advancedExpanded
                    Keys.onReturnPressed: advancedExpanded = !advancedExpanded
                    Keys.onEnterPressed: advancedExpanded = !advancedExpanded
                }

                // --- Advanced Controls (collapsible) ---
                ColumnLayout {
                    id: advancedContent
                    visible: advancedToggle.advancedExpanded
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    Layout.leftMargin: Theme.spacingMedium

                    RowLayout {
                        id: hdrOutputModeRow
                        Layout.fillWidth: true
                        spacing: Theme.spacingMedium

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingXSmall

                            Text {
                                text: qsTr("HDR Output Mode")
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary
                            }

                            Text {
                                text: qsTr("Match content keeps SDR in SDR and HDR in HDR when supported")
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        SettingsComboBox {
                            id: hdrOutputModeCombo
                            model: root.hdrOutputModeLabels
                            currentIndex: root.indexForValue(root.hdrOutputModeValues, ConfigManager.hdrOutputMode)
                            Layout.preferredWidth: Math.round(260 * Theme.layoutScale)

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root._lastFocusedItem = this
                                    flickable.ensureFocusVisible(this)
                                }
                            }

                            onActivated: function(index) {
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

                    RowLayout {
                        id: dolbyVisionFallbackRow
                        Layout.fillWidth: true
                        spacing: Theme.spacingMedium

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingXSmall

                            Text {
                                text: qsTr("Dolby Vision Fallback")
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary
                            }

                            Text {
                                text: qsTr("Unsupported Dolby Vision is locally tone-mapped instead of transcoded")
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        SettingsComboBox {
                            id: dolbyVisionFallbackCombo
                            model: root.dolbyVisionFallbackModeLabels
                            currentIndex: root.indexForValue(root.dolbyVisionFallbackModeValues, ConfigManager.dolbyVisionFallbackMode)
                            Layout.preferredWidth: Math.round(280 * Theme.layoutScale)

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root._lastFocusedItem = this
                                    flickable.ensureFocusVisible(this)
                                }
                            }

                            onActivated: function(index) {
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
                                    linuxRefreshRateInput.input.forceActiveFocus()
                                    event.accepted = true
                                }
                            }
                            Keys.onReturnPressed: popup.open()
                            Keys.onEnterPressed: popup.open()
                        }
                    }

                    SettingsTextInputRow {
                        id: linuxRefreshRateInput
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
                        keyUpTarget: linuxHDRInput.input
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
