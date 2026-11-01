import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()
    readonly property bool scrolled: flickable.scrolled
    readonly property Item preferredEntryItem: autoplaySwitch

    function enterFromRail() {
        if (_lastFocusedItem && _lastFocusedItem.visible) {
            _lastFocusedItem.forceActiveFocus()
        } else {
            autoplaySwitch.forceActiveFocus()
        }
    }

    function restoreFocus() {
        enterFromRail()
    }

    property Item _lastFocusedItem: null
    function defaultTrackOptions(includeOff) {
        var options = [
            { label: qsTr("Server Default"), value: "jellyfin-default" },
            { label: qsTr("File Default"), value: "file-default" }
        ]
        if (includeOff) {
            options.push({ label: qsTr("Off"), value: "off" })
            options.push({ label: qsTr("Forced"), value: "forced" })
        }
        return options.concat(ConfigManager.supportedTrackLanguages)
    }

    function optionIndexForValue(options, value) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].value === value) return i
        }
        return 0
    }

    // Builds the option list for the audio output device selector from
    // PlayerController.audioOutputOptions: System Default, the active audio
    // driver's endpoints, and a saved device that is currently missing (shown
    // so the preference stays visible instead of being replaced).
    function audioOutputDeviceOptions() {
        var source = PlayerController.audioOutputOptions
        var options = []
        for (var i = 0; i < source.length; ++i) {
            var entry = source[i]
            var label = entry.kind === "default"
                    ? qsTr("System Default")
                    : ((entry.description && entry.description.length > 0) ? entry.description : entry.id)
            if (!entry.available) label = qsTr("%1 (not connected)").arg(label)
            options.push({ label: label, value: entry.id, selected: entry.selected })
        }
        if (options.length === 0) options.push({ label: qsTr("System Default"), value: "auto", selected: true })
        return options
    }

    function selectedOptionIndex(options) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].selected) return i
        }
        return 0
    }

    function setAudioPassthroughCodec(codec, enabled) {
        var codecs = ConfigManager.audioPassthroughCodecs.slice()
        var index = codecs.indexOf(codec)
        if (enabled && index < 0) {
            codecs.push(codec)
        } else if (!enabled && index >= 0) {
            codecs.splice(index, 1)
        }
        ConfigManager.audioPassthroughCodecs = codecs
    }

    // Values are strings because ChoiceComboRow matches them with ===.
    readonly property var stillWatchingOptions: [
        { label: qsTr("Off"), value: "0" },
        { label: qsTr("2 episodes"), value: "2" },
        { label: qsTr("3 episodes"), value: "3" },
        { label: qsTr("4 episodes"), value: "4" },
        { label: qsTr("5 episodes"), value: "5" }
    ]
    readonly property var sleepTimerOptions: [
        { label: qsTr("Off"), value: "0" },
        { label: qsTr("15 min"), value: "15" },
        { label: qsTr("30 min"), value: "30" },
        { label: qsTr("45 min"), value: "45" },
        { label: qsTr("60 min"), value: "60" },
        { label: qsTr("90 min"), value: "90" },
        { label: qsTr("End of episode or movie"), value: "-1" }
    ]
    readonly property var versionPreferenceOptions: [
        { label: qsTr("Highest Quality"), value: "highest-quality" },
        { label: qsTr("Smallest"), value: "smallest" },
        { label: qsTr("Ask Every Time"), value: "ask" }
    ]
    readonly property var globalStartupBufferingOptions: [
        { label: qsTr("Normal"), value: "normal" },
        { label: qsTr("Remote Mount"), value: "remote-mount" }
    ]
    readonly property var libraryStartupBufferingOptions: [
        { label: qsTr("Use Default"), value: "" },
        { label: qsTr("Normal"), value: "normal" },
        { label: qsTr("Remote Mount"), value: "remote-mount" }
    ]

    component ChoiceComboRow: SettingsRow {
        id: choiceRow

        property var options: []
        property string selectedValue: "normal"
        property Item upTarget: null
        property Item downTarget: null
        property bool initialized: false
        property bool updatingSelection: false
        property alias focusItem: combo
        signal selected(string value)

        highlighted: combo.activeFocus || combo.popup.visible
        focusTarget: combo
        onActivated: combo.popup.open()

        function refreshSelection() {
            var idx = root.optionIndexForValue(options, selectedValue)
            if (combo.currentIndex === idx) return
            updatingSelection = true
            combo.currentIndex = idx
            updatingSelection = false
        }

        SettingsComboBox {
            id: combo
            model: choiceRow.options
            textRole: "label"
            valueRole: "value"
            currentIndex: root.optionIndexForValue(choiceRow.options, choiceRow.selectedValue)
            focusPolicy: Qt.StrongFocus

            Component.onCompleted: {
                choiceRow.refreshSelection()
                choiceRow.initialized = true
            }

            onActiveFocusChanged: {
                if (activeFocus) {
                    root._lastFocusedItem = this
                    flickable.ensureFocusVisible(this)
                    InputModeManager.setNavigationMode("keyboard")
                    InputModeManager.hideCursor(true)
                } else if (!popup.visible) {
                    InputModeManager.setNavigationMode("pointer")
                    InputModeManager.hideCursor(false)
                }
            }

            onCurrentIndexChanged: {
                if (!choiceRow.initialized || choiceRow.updatingSelection || currentIndex < 0) return
                choiceRow.selected(choiceRow.options[currentIndex].value)
            }

            Keys.onUpPressed: function(event) {
                if (!popup.visible && choiceRow.upTarget) {
                    choiceRow.upTarget.forceActiveFocus()
                    event.accepted = true
                }
            }
            Keys.onDownPressed: function(event) {
                if (!popup.visible && choiceRow.downTarget) {
                    choiceRow.downTarget.forceActiveFocus()
                    event.accepted = true
                }
            }
            Keys.onReturnPressed: function(event) { popup.open(); event.accepted = true }
            Keys.onEnterPressed: function(event) { popup.open(); event.accepted = true }
        }
    }

    Keys.priority: Keys.AfterItem
    Keys.onLeftPressed: function(event) {
        root.requestReturnToRail()
        event.accepted = true
    }
    Keys.onEscapePressed: function(event) {
        root.requestReturnToRail()
        event.accepted = true
    }

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
                    text: qsTr("Autoplay")
                    Layout.fillWidth: true
                    Layout.topMargin: 0
                }

                SettingsToggleRow {
                    id: autoplaySwitch
                    label: qsTr("Autoplay Next Episode")
                    description: qsTr("Automatically play the next episode when the current one ends")
                    checked: ConfigManager.autoplayNextEpisode
                    Layout.fillWidth: true
                    focus: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.down: mergeHomeRowsSwitch

                    onToggled: function(value) {
                        ConfigManager.autoplayNextEpisode = value
                    }
                }

                SettingsToggleRow {
                    id: mergeHomeRowsSwitch
                    label: qsTr("Merge Continue Watching with Next Up")
                    description: qsTr("When enabled, in-progress movies and episodes appear in the Next Up row instead of a separate Continue Watching section on Home")
                    checked: ConfigManager.mergeContinueWatchingWithNextUp
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: autoplaySwitch
                    KeyNavigation.down: autoplayCountdownCombo.enabled ? autoplayCountdownCombo : stillWatchingRow.focusItem

                    onToggled: function(value) {
                        ConfigManager.mergeContinueWatchingWithNextUp = value
                    }
                }

                SettingsRow {
                    id: autoplayCountdownRow
                    label: qsTr("Autoplay Countdown")
                    description: qsTr("Seconds before next episode auto-plays")
                    dimmed: !autoplayCountdownCombo.enabled
                    highlighted: autoplayCountdownCombo.activeFocus || autoplayCountdownCombo.popup.visible
                    focusTarget: autoplayCountdownCombo
                    onActivated: if (autoplayCountdownCombo.enabled) autoplayCountdownCombo.popup.open()

                    SettingsComboBox {
                        id: autoplayCountdownCombo
                        model: ["5s", "10s", "15s", "20s", "25s", "30s"]
                        currentIndex: 0
                        focusPolicy: Qt.StrongFocus
                        enabled: ConfigManager.autoplayNextEpisode
                        property bool initialized: false
                        property bool updatingSelection: false

                        function configSecondsToIndex(seconds) {
                            return Math.max(0, Math.min(
                                Math.round((seconds - 5) / 5),
                                model.length - 1
                            ))
                        }

                        function refreshSelectionFromConfig() {
                            var idx = configSecondsToIndex(ConfigManager.autoplayCountdownSeconds)
                            if (currentIndex === idx) return
                            updatingSelection = true
                            currentIndex = idx
                            updatingSelection = false
                        }

                        Component.onCompleted: {
                            refreshSelectionFromConfig()
                            initialized = true
                        }

                        Connections {
                            target: ConfigManager
                            function onAutoplayCountdownSecondsChanged() {
                                autoplayCountdownCombo.refreshSelectionFromConfig()
                            }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                            } else if (!popup.visible) {
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (!initialized || updatingSelection) return
                            var seconds = (currentIndex * 5) + 5
                            if (seconds !== ConfigManager.autoplayCountdownSeconds) {
                                ConfigManager.autoplayCountdownSeconds = seconds
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                mergeHomeRowsSwitch.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                stillWatchingRow.focusItem.forceActiveFocus()
                                event.accepted = true
                            }
                        }

                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()
                    }
                }

                ChoiceComboRow {
                    id: stillWatchingRow
                    label: qsTr("Ask If I'm Still Watching")
                    description: qsTr("After this many episodes autoplay in a row with no input, ask before playing the next one")
                    options: root.stillWatchingOptions
                    selectedValue: String(ConfigManager.stillWatchingEpisodeThreshold)
                    upTarget: autoplayCountdownCombo.enabled ? autoplayCountdownCombo : mergeHomeRowsSwitch
                    downTarget: sleepTimerDefaultRow.focusItem

                    onSelected: function(value) {
                        ConfigManager.stillWatchingEpisodeThreshold = parseInt(value, 10)
                    }

                    Connections {
                        target: ConfigManager
                        function onStillWatchingEpisodeThresholdChanged() {
                            stillWatchingRow.selectedValue = String(ConfigManager.stillWatchingEpisodeThreshold)
                            stillWatchingRow.refreshSelection()
                        }
                    }
                }

                ChoiceComboRow {
                    id: sleepTimerDefaultRow
                    label: qsTr("Default Sleep Timer")
                    description: qsTr("Sleep timer each new playback starts with; change it during playback in Playback Settings")
                    options: root.sleepTimerOptions
                    selectedValue: String(ConfigManager.sleepTimerDefault)
                    upTarget: stillWatchingRow.focusItem
                    downTarget: thresholdSlider

                    onSelected: function(value) {
                        ConfigManager.sleepTimerDefault = parseInt(value, 10)
                    }

                    Connections {
                        target: ConfigManager
                        function onSleepTimerDefaultChanged() {
                            sleepTimerDefaultRow.selectedValue = String(ConfigManager.sleepTimerDefault)
                            sleepTimerDefaultRow.refreshSelection()
                        }
                    }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Progress")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsSliderRow {
                    id: thresholdSlider
                    label: qsTr("Mark as Watched")
                    description: qsTr("Percentage of playback required to mark as watched")
                    value: ConfigManager.playbackCompletionThreshold
                    from: 50
                    to: 100
                    stepSize: 5
                    unit: "%"
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: sleepTimerDefaultRow.focusItem
                    KeyNavigation.down: versionPreferenceRow.focusItem

                    onSliderValueChanged: function(newValue) {
                        ConfigManager.playbackCompletionThreshold = newValue
                    }
                }

                ChoiceComboRow {
                    id: versionPreferenceRow
                    label: qsTr("Preferred Version")
                    description: qsTr("Which version plays when an item has more than one")
                    options: root.versionPreferenceOptions
                    selectedValue: ConfigManager.playbackVersionPreference
                    upTarget: thresholdSlider
                    downTarget: audioDelaySpinBox

                    onSelected: function(value) {
                        ConfigManager.playbackVersionPreference = value
                    }

                    Connections {
                        target: ConfigManager
                        function onPlaybackVersionPreferenceChanged() {
                            versionPreferenceRow.selectedValue = ConfigManager.playbackVersionPreference
                            versionPreferenceRow.refreshSelection()
                        }
                    }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Audio")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsSpinBoxRow {
                    id: audioDelaySpinBox
                    label: qsTr("Audio Delay")
                    description: qsTr("Adjust audio synchronization (positive delays audio, negative advances it)")
                    value: ConfigManager.audioDelay
                    from: -5000
                    to: 5000
                    stepSize: 50
                    unit: "ms"
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: versionPreferenceRow.focusItem
                    KeyNavigation.down: audioOutputDeviceCombo

                    onSpinBoxValueChanged: function(newValue) {
                        ConfigManager.audioDelay = newValue
                    }
                }

                SettingsRow {
                    id: audioOutputDeviceRow
                    label: qsTr("Audio Output Device")
                    description: qsTr("Choose where audio plays. \"System Default\" follows the system. A saved device that is disconnected is kept, and playback uses the system default until it returns.")
                    dimmed: !audioOutputDeviceCombo.enabled
                    highlighted: audioOutputDeviceCombo.activeFocus || audioOutputDeviceCombo.popup.visible
                    focusTarget: audioOutputDeviceCombo
                    onActivated: if (audioOutputDeviceCombo.enabled) audioOutputDeviceCombo.popup.open()

                    SettingsComboBox {
                        id: audioOutputDeviceCombo
                        property var options: root.audioOutputDeviceOptions()
                        model: options
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.selectedOptionIndex(options)
                        focusPolicy: Qt.StrongFocus

                        function refreshOptions() {
                            options = root.audioOutputDeviceOptions()
                            currentIndex = root.selectedOptionIndex(options)
                        }

                        Component.onCompleted: {
                            refreshOptions()
                            // Re-enumerate so endpoints connected since startup appear.
                            PlayerController.refreshAudioOutputDevices()
                        }

                        Connections {
                            target: ConfigManager
                            function onAudioOutputDeviceChanged() {
                                audioOutputDeviceCombo.refreshOptions()
                            }
                        }

                        Connections {
                            target: PlayerController
                            function onAudioOutputStatusChanged() {
                                if (!audioOutputDeviceCombo.popup.visible) audioOutputDeviceCombo.refreshOptions()
                            }
                        }

                        Connections {
                            target: audioOutputDeviceCombo.popup
                            function onAboutToShow() { PlayerController.refreshAudioOutputDevices() }
                            function onClosed() { audioOutputDeviceCombo.refreshOptions() }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                            } else if (!popup.visible) {
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }
                        }

                        onSelectionAccepted: function(index) {
                            if (index < 0 || index >= options.length) return
                            PlayerController.selectAudioOutputDevice(options[index].value)
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                audioDelaySpinBox.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                if (audioExclusiveToggle.visible) {
                                    audioExclusiveToggle.forceActiveFocus()
                                } else {
                                    audioPassthroughModeCombo.forceActiveFocus()
                                }
                                event.accepted = true
                            }
                        }
                    }
                }

                SettingsToggleRow {
                    id: audioExclusiveToggle
                    label: qsTr("Exclusive Mode")
                    description: qsTr("Bloom takes sole control of the output during playback and bypasses the Windows mixer, so other apps are silent. The original format is used when the device accepts it; this is not a guarantee of bit-perfect output. If the device refuses, playback continues in shared mode.")
                    checked: ConfigManager.audioExclusiveMode
                    visible: PlayerController.audioOutputStatus.exclusiveSupported === true
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: audioOutputDeviceCombo
                    KeyNavigation.down: audioPassthroughModeCombo
                    onToggled: function(value) { ConfigManager.audioExclusiveMode = value }
                }

                SettingsRow {
                    id: audioPassthroughRow
                    label: qsTr("Audio Passthrough")
                    description: qsTr("Choose which surround sound formats Bloom can send directly to your audio device.")
                    dimmed: !audioPassthroughModeCombo.enabled
                    highlighted: audioPassthroughModeCombo.activeFocus || audioPassthroughModeCombo.popup.visible
                    focusTarget: audioPassthroughModeCombo
                    onActivated: if (audioPassthroughModeCombo.enabled) audioPassthroughModeCombo.popup.open()

                    SettingsComboBox {
                        id: audioPassthroughModeCombo
                        readonly property var options: [
                            { label: qsTr("Automatic"), value: "auto" },
                            { label: qsTr("Disabled"), value: "disabled" },
                            { label: qsTr("Custom"), value: "custom" }
                        ]
                        model: options
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.optionIndexForValue(options, ConfigManager.audioPassthroughMode)
                        focusPolicy: Qt.StrongFocus
                        property bool initialized: false
                        property bool updatingSelection: false

                        function refreshSelection() {
                            updatingSelection = true
                            currentIndex = root.optionIndexForValue(options, ConfigManager.audioPassthroughMode)
                            updatingSelection = false
                        }

                        Component.onCompleted: {
                            refreshSelection()
                            initialized = true
                        }

                        Connections {
                            target: ConfigManager
                            function onAudioPassthroughChanged() {
                                audioPassthroughModeCombo.refreshSelection()
                            }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                            } else if (!popup.visible) {
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (!initialized || updatingSelection || currentIndex < 0) return
                            var value = options[currentIndex].value
                            if (value !== ConfigManager.audioPassthroughMode) {
                                ConfigManager.audioPassthroughMode = value
                            }
                        }

                        KeyNavigation.up: audioExclusiveToggle.visible ? audioExclusiveToggle : audioOutputDeviceCombo
                        KeyNavigation.down: ConfigManager.audioPassthroughMode === "custom"
                            ? audioPassthroughAc3Toggle : defaultAudioTrackCombo
                    }
                }

                SettingsToggleRow {
                    id: audioPassthroughAc3Toggle
                    label: qsTr("Dolby Digital")
                    description: qsTr("Allow Dolby Digital audio passthrough")
                    checked: ConfigManager.audioPassthroughCodecs.indexOf("ac3") >= 0
                    visible: ConfigManager.audioPassthroughMode === "custom"
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: audioPassthroughModeCombo
                    KeyNavigation.down: audioPassthroughEac3Toggle
                    onToggled: function(value) { root.setAudioPassthroughCodec("ac3", value) }
                }

                SettingsToggleRow {
                    id: audioPassthroughEac3Toggle
                    label: qsTr("Dolby Digital Plus")
                    description: qsTr("Allow Dolby Digital Plus audio passthrough")
                    checked: ConfigManager.audioPassthroughCodecs.indexOf("eac3") >= 0
                    visible: ConfigManager.audioPassthroughMode === "custom"
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: audioPassthroughAc3Toggle
                    KeyNavigation.down: audioPassthroughTruehdToggle
                    onToggled: function(value) { root.setAudioPassthroughCodec("eac3", value) }
                }

                SettingsToggleRow {
                    id: audioPassthroughTruehdToggle
                    label: qsTr("Dolby TrueHD")
                    description: qsTr("Allow Dolby TrueHD audio passthrough")
                    checked: ConfigManager.audioPassthroughCodecs.indexOf("truehd") >= 0
                    visible: ConfigManager.audioPassthroughMode === "custom"
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: audioPassthroughEac3Toggle
                    KeyNavigation.down: audioPassthroughDtsToggle
                    onToggled: function(value) { root.setAudioPassthroughCodec("truehd", value) }
                }

                SettingsToggleRow {
                    id: audioPassthroughDtsToggle
                    label: qsTr("DTS")
                    description: qsTr("Allow DTS audio passthrough")
                    checked: ConfigManager.audioPassthroughCodecs.indexOf("dts") >= 0
                    visible: ConfigManager.audioPassthroughMode === "custom"
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: audioPassthroughTruehdToggle
                    KeyNavigation.down: audioPassthroughDtshdToggle
                    onToggled: function(value) { root.setAudioPassthroughCodec("dts", value) }
                }

                SettingsToggleRow {
                    id: audioPassthroughDtshdToggle
                    label: qsTr("DTS-HD")
                    description: qsTr("Allow DTS-HD audio passthrough")
                    checked: ConfigManager.audioPassthroughCodecs.indexOf("dts-hd") >= 0
                    visible: ConfigManager.audioPassthroughMode === "custom"
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: audioPassthroughDtsToggle
                    KeyNavigation.down: defaultAudioTrackCombo
                    onToggled: function(value) { root.setAudioPassthroughCodec("dts-hd", value) }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Languages")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsRow {
                    id: defaultAudioTrackRow
                    label: qsTr("Audio Language")
                    description: qsTr("Fallback used when no season or movie audio preference is saved")
                    dimmed: !defaultAudioTrackCombo.enabled
                    highlighted: defaultAudioTrackCombo.activeFocus || defaultAudioTrackCombo.popup.visible
                    focusTarget: defaultAudioTrackCombo
                    onActivated: if (defaultAudioTrackCombo.enabled) defaultAudioTrackCombo.popup.open()

                    SettingsComboBox {
                        id: defaultAudioTrackCombo
                        readonly property var options: root.defaultTrackOptions(false)
                        model: options
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.optionIndexForValue(options, ConfigManager.defaultAudioTrackSelection)
                        focusPolicy: Qt.StrongFocus
                        property bool initialized: false
                        property bool updatingSelection: false

                        function refreshSelectionFromConfig() {
                            var idx = root.optionIndexForValue(options, ConfigManager.defaultAudioTrackSelection)
                            if (currentIndex === idx) return
                            updatingSelection = true
                            currentIndex = idx
                            updatingSelection = false
                        }

                        Component.onCompleted: {
                            refreshSelectionFromConfig()
                            initialized = true
                        }

                        Connections {
                            target: ConfigManager
                            function onDefaultAudioTrackSelectionChanged() {
                                defaultAudioTrackCombo.refreshSelectionFromConfig()
                            }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                            } else if (!popup.visible) {
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (!initialized || updatingSelection || currentIndex < 0) return
                            var value = options[currentIndex].value
                            if (value !== ConfigManager.defaultAudioTrackSelection) {
                                ConfigManager.defaultAudioTrackSelection = value
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                if (ConfigManager.audioPassthroughMode === "custom") {
                                    audioPassthroughDtshdToggle.forceActiveFocus()
                                } else {
                                    audioPassthroughModeCombo.forceActiveFocus()
                                }
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                defaultSubtitleTrackCombo.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onReturnPressed: function(event) {
                            popup.open()
                            event.accepted = true
                        }
                        Keys.onEnterPressed: function(event) {
                            popup.open()
                            event.accepted = true
                        }
                    }
                }

                SettingsRow {
                    id: defaultSubtitleTrackRow
                    label: qsTr("Subtitle Language")
                    description: qsTr("Fallback used when no season or movie subtitle preference is saved")
                    dimmed: !defaultSubtitleTrackCombo.enabled
                    highlighted: defaultSubtitleTrackCombo.activeFocus || defaultSubtitleTrackCombo.popup.visible
                    focusTarget: defaultSubtitleTrackCombo
                    onActivated: if (defaultSubtitleTrackCombo.enabled) defaultSubtitleTrackCombo.popup.open()

                    SettingsComboBox {
                        id: defaultSubtitleTrackCombo
                        readonly property var options: root.defaultTrackOptions(true)
                        model: options
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.optionIndexForValue(options, ConfigManager.defaultSubtitleTrackSelection)
                        focusPolicy: Qt.StrongFocus
                        property bool initialized: false
                        property bool updatingSelection: false

                        function refreshSelectionFromConfig() {
                            var idx = root.optionIndexForValue(options, ConfigManager.defaultSubtitleTrackSelection)
                            if (currentIndex === idx) return
                            updatingSelection = true
                            currentIndex = idx
                            updatingSelection = false
                        }

                        Component.onCompleted: {
                            refreshSelectionFromConfig()
                            initialized = true
                        }

                        Connections {
                            target: ConfigManager
                            function onDefaultSubtitleTrackSelectionChanged() {
                                defaultSubtitleTrackCombo.refreshSelectionFromConfig()
                            }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                            } else if (!popup.visible) {
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (!initialized || updatingSelection || currentIndex < 0) return
                            var value = options[currentIndex].value
                            if (value !== ConfigManager.defaultSubtitleTrackSelection) {
                                ConfigManager.defaultSubtitleTrackSelection = value
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                defaultAudioTrackCombo.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                skipPopupDurationSlider.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onReturnPressed: function(event) {
                            popup.open()
                            event.accepted = true
                        }
                        Keys.onEnterPressed: function(event) {
                            popup.open()
                            event.accepted = true
                        }
                    }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Intros and Credits")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsSliderRow {
                    id: skipPopupDurationSlider
                    label: qsTr("Skip Intro/Credits Popup Duration")
                    description: qsTr("How long the playback skip popup stays visible (0 disables popup)")
                    value: ConfigManager.skipButtonAutoHideSeconds
                    from: 0
                    to: 120
                    stepSize: 1
                    unit: "s"
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: defaultSubtitleTrackCombo
                    KeyNavigation.down: autoSkipIntroToggle

                    onSliderValueChanged: function(newValue) {
                        ConfigManager.skipButtonAutoHideSeconds = newValue
                    }
                }

                SettingsToggleRow {
                    id: autoSkipIntroToggle
                    label: qsTr("Auto Skip Intro")
                    description: qsTr("Automatically skip intro once when it first appears")
                    checked: ConfigManager.autoSkipIntro
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: skipPopupDurationSlider
                    KeyNavigation.down: autoSkipOutroToggle

                    onToggled: function(value) {
                        ConfigManager.autoSkipIntro = value
                    }
                }

                SettingsToggleRow {
                    id: autoSkipOutroToggle
                    label: qsTr("Auto Skip Credits")
                    description: qsTr("Automatically skip credits once when they first appear")
                    checked: ConfigManager.autoSkipOutro
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: autoSkipIntroToggle
                    KeyNavigation.down: serverTrickplayToggle

                    onToggled: function(value) {
                        ConfigManager.autoSkipOutro = value
                    }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Seek Previews")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsToggleRow {
                    id: serverTrickplayToggle
                    label: qsTr("Server Trickplay")
                    description: qsTr("Show seek previews from the server's trickplay images. Applies from the next playback")
                    checked: ConfigManager.serverTrickplayEnabled
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: autoSkipOutroToggle
                    KeyNavigation.down: localSeekPreviewsToggle

                    onToggled: function(value) {
                        ConfigManager.serverTrickplayEnabled = value
                    }
                }

                SettingsToggleRow {
                    id: localSeekPreviewsToggle
                    label: qsTr("Generate Seek Previews")
                    description: qsTr("Build previews from the video when server trickplay is unavailable. Adds extra reads from the server. Applies from the next playback")
                    checked: ConfigManager.localSeekPreviewsEnabled
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: serverTrickplayToggle
                    KeyNavigation.down: uiSoundsToggle

                    onToggled: function(value) {
                        ConfigManager.localSeekPreviewsEnabled = value
                    }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Sounds")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsToggleRow {
                    id: uiSoundsToggle
                    label: qsTr("UI Sounds")
                    description: qsTr("Play a sound on navigation, select, and back actions")
                    checked: ConfigManager.uiSoundsEnabled
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: localSeekPreviewsToggle
                    KeyNavigation.down: uiSoundsVolumeCombo.enabled ? uiSoundsVolumeCombo : themeSongVolumeCombo

                    onToggled: function(value) {
                        ConfigManager.uiSoundsEnabled = value
                    }
                }

                SettingsRow {
                    id: uiSoundsVolumeRow
                    label: qsTr("UI Sound Volume")
                    description: qsTr("Click feedback for navigation and selection")
                    dimmed: !uiSoundsVolumeCombo.enabled
                    highlighted: uiSoundsVolumeCombo.activeFocus || uiSoundsVolumeCombo.popup.visible
                    focusTarget: uiSoundsVolumeCombo
                    onActivated: if (uiSoundsVolumeCombo.enabled) uiSoundsVolumeCombo.popup.open()

                    SettingsComboBox {
                        id: uiSoundsVolumeCombo
                        model: [qsTr("Off"), qsTr("Very Low"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                        currentIndex: 0
                        focusPolicy: Qt.StrongFocus
                        enabled: ConfigManager.uiSoundsEnabled
                        property bool initialized: false
                        property bool updatingSelection: false

                        function refreshSelectionFromConfig() {
                            if (currentIndex === ConfigManager.uiSoundsVolume) return
                            updatingSelection = true
                            currentIndex = ConfigManager.uiSoundsVolume
                            updatingSelection = false
                        }

                        Component.onCompleted: {
                            refreshSelectionFromConfig()
                            initialized = true
                        }

                        Connections {
                            target: ConfigManager
                            function onUiSoundsVolumeChanged() {
                                uiSoundsVolumeCombo.refreshSelectionFromConfig()
                            }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                            } else if (!popup.visible) {
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (!initialized || updatingSelection) return
                            if (currentIndex !== ConfigManager.uiSoundsVolume) {
                                ConfigManager.uiSoundsVolume = currentIndex
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                uiSoundsToggle.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                themeSongVolumeCombo.forceActiveFocus()
                                event.accepted = true
                            }
                        }

                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()
                    }
                }

                SettingsRow {
                    id: themeSongVolumeRow
                    label: qsTr("Theme Song Volume")
                    description: qsTr("Play series theme music when browsing")
                    dimmed: !themeSongVolumeCombo.enabled
                    highlighted: themeSongVolumeCombo.activeFocus || themeSongVolumeCombo.popup.visible
                    focusTarget: themeSongVolumeCombo
                    onActivated: if (themeSongVolumeCombo.enabled) themeSongVolumeCombo.popup.open()

                    SettingsComboBox {
                        id: themeSongVolumeCombo
                        model: [qsTr("Off"), qsTr("Very Low"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                        currentIndex: 0
                        focusPolicy: Qt.StrongFocus
                        property bool initialized: false
                        property bool updatingSelection: false

                        function refreshSelectionFromConfig() {
                            if (currentIndex === ConfigManager.themeSongVolume) return
                            updatingSelection = true
                            currentIndex = ConfigManager.themeSongVolume
                            updatingSelection = false
                        }

                        Component.onCompleted: {
                            refreshSelectionFromConfig()
                            initialized = true
                        }

                        Connections {
                            target: ConfigManager
                            function onThemeSongVolumeChanged() {
                                themeSongVolumeCombo.refreshSelectionFromConfig()
                            }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                            } else if (!popup.visible) {
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (!initialized || updatingSelection) return
                            if (currentIndex !== ConfigManager.themeSongVolume) {
                                ConfigManager.themeSongVolume = currentIndex
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                if (uiSoundsVolumeCombo.enabled) uiSoundsVolumeCombo.forceActiveFocus()
                                else uiSoundsToggle.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                themeSongLoopToggle.forceActiveFocus()
                                event.accepted = true
                            }
                        }

                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()
                    }
                }

                SettingsToggleRow {
                    id: themeSongLoopToggle
                    label: qsTr("Loop Theme Song")
                    description: qsTr("Loop theme music while browsing a series")
                    checked: ConfigManager.themeSongLoop
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: themeSongVolumeCombo
                    KeyNavigation.down: cacheSizeSpinBox

                    onToggled: function(value) {
                        ConfigManager.themeSongLoop = value
                    }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Buffering")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsSpinBoxRow {
                    id: cacheSizeSpinBox
                    label: qsTr("Playback Cache Size")
                    description: qsTr("Amount of memory to use for buffering video during playback")
                    value: ConfigManager.playbackCacheSizeMB
                    from: 50
                    to: 2048
                    stepSize: 50
                    unit: "MB"
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: themeSongLoopToggle
                    KeyNavigation.down: globalStartupBufferingRow.focusItem

                    onSpinBoxValueChanged: function(newValue) {
                        ConfigManager.playbackCacheSizeMB = newValue
                    }
                }

                ChoiceComboRow {
                    id: globalStartupBufferingRow
                    label: qsTr("Startup Buffering")
                    description: qsTr("Remote Mount waits for a larger media cache before playback starts")
                    options: root.globalStartupBufferingOptions
                    selectedValue: ConfigManager.startupBufferingMode
                    upTarget: cacheSizeSpinBox
                    downTarget: libraryStartupBufferingRepeater.count > 0
                                ? libraryStartupBufferingRepeater.itemAt(0).focusItem
                                : null

                    onSelected: function(value) {
                        ConfigManager.startupBufferingMode = value
                    }

                    Connections {
                        target: ConfigManager
                        function onStartupBufferingModeChanged() {
                            globalStartupBufferingRow.selectedValue = ConfigManager.startupBufferingMode
                            globalStartupBufferingRow.refreshSelection()
                        }
                    }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Library Overrides")
                    detail: qsTr("Startup buffering per library")
                    visible: libraryStartupBufferingRepeater.count > 0
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                Repeater {
                    id: libraryStartupBufferingRepeater
                    model: LibraryViewModel.views

                    delegate: ChoiceComboRow {
                        required property var modelData
                        required property int index

                        property string libraryName: modelData.name || ""
                        property string libraryId: modelData.itemId || ""

                        label: libraryName
                        description: qsTr("Startup buffering for this library")
                        options: root.libraryStartupBufferingOptions
                        selectedValue: ConfigManager.getLibraryStartupBufferingMode(libraryId)
                        upTarget: index > 0
                                  ? libraryStartupBufferingRepeater.itemAt(index - 1).focusItem
                                  : globalStartupBufferingRow.focusItem
                        downTarget: index < libraryStartupBufferingRepeater.count - 1
                                    ? libraryStartupBufferingRepeater.itemAt(index + 1).focusItem
                                    : null

                        onSelected: function(value) {
                            ConfigManager.setLibraryStartupBufferingMode(libraryId, value)
                        }

                        Connections {
                            target: ConfigManager
                            function onLibraryStartupBufferingModesChanged() {
                                selectedValue = ConfigManager.getLibraryStartupBufferingMode(libraryId)
                                refreshSelection()
                            }
                        }
                    }
                }
            }
        }
    }

    // Outside the layout so it can overlay the scroll view.
    WheelStepScroller {
        x: flickable.x
        y: flickable.y
        width: flickable.width
        height: flickable.height
        target: flickable.view
        stepPx: Math.round(88 * Theme.layoutScale)
    }
}
