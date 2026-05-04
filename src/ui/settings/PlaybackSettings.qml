import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()
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
            { label: qsTr("Jellyfin Default"), value: "jellyfin-default" },
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

    Keys.priority: Keys.AfterItem
    Keys.onLeftPressed: function(event) {
        root.requestReturnToRail()
        event.accepted = true
    }
    Keys.onEscapePressed: function(event) {
        root.requestReturnToRail()
        event.accepted = true
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
            text: qsTr("Playback")
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

                // ── Group 1: Autoplay ──

                SettingsToggleRow {
                    id: autoplaySwitch
                    label: qsTr("Autoplay Next Episode")
                    description: qsTr("Automatically play the next episode when the current one ends")
                    checked: ConfigManager.autoplayNextEpisode
                    Layout.fillWidth: true
                    focus: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.down: autoplayCountdownCombo.enabled ? autoplayCountdownCombo : thresholdSlider

                    onToggled: function(value) {
                        ConfigManager.autoplayNextEpisode = value
                    }
                }

                RowLayout {
                    id: autoplayCountdownRow
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        spacing: Math.round(4 * Theme.layoutScale)
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Autoplay Countdown")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: autoplayCountdownCombo.enabled ? Theme.textPrimary : Theme.textDisabled
                        }

                        Text {
                            text: qsTr("Seconds before next episode auto-plays")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: autoplayCountdownCombo.enabled ? Theme.textSecondary : Theme.textDisabled
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    ComboBox {
                        id: autoplayCountdownCombo
                        model: ["5s", "10s", "15s", "20s", "25s", "30s"]
                        currentIndex: 0
                        Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
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
                                autoplaySwitch.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                thresholdSlider.forceActiveFocus()
                                event.accepted = true
                            }
                        }

                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()

                        background: Rectangle {
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: Theme.inputBackground
                            border.color: autoplayCountdownCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: autoplayCountdownCombo.activeFocus ? 2 : 1
                            opacity: autoplayCountdownCombo.enabled ? 1.0 : 0.5
                        }

                        contentItem: Text {
                            text: autoplayCountdownCombo.displayText
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: autoplayCountdownCombo.enabled ? Theme.textPrimary : Theme.textDisabled
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                        }

                        delegate: ItemDelegate {
                            width: autoplayCountdownCombo.width
                            enabled: autoplayCountdownCombo.enabled
                            contentItem: Text {
                                text: modelData
                                color: highlighted ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: highlighted ? Theme.buttonPrimaryBackground : "transparent"
                                radius: Theme.radiusSmall
                            }
                            highlighted: ListView.isCurrentItem || autoplayCountdownCombo.highlightedIndex === index
                        }

                        popup: Popup {
                            y: autoplayCountdownCombo.height + 5
                            width: autoplayCountdownCombo.width
                            implicitHeight: contentItem.implicitHeight
                            padding: 1

                            onOpened: {
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                                autoplayCountdownList.currentIndex = autoplayCountdownCombo.highlightedIndex >= 0
                                    ? autoplayCountdownCombo.highlightedIndex
                                    : autoplayCountdownCombo.currentIndex
                                autoplayCountdownList.forceActiveFocus()
                            }
                            onClosed: {
                                autoplayCountdownCombo.forceActiveFocus()
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }

                            contentItem: ListView {
                                id: autoplayCountdownList
                                clip: true
                                implicitHeight: contentHeight
                                model: autoplayCountdownCombo.popup.visible ? autoplayCountdownCombo.delegateModel : null
                                currentIndex: autoplayCountdownCombo.highlightedIndex >= 0
                                    ? autoplayCountdownCombo.highlightedIndex
                                    : autoplayCountdownCombo.currentIndex

                                ScrollIndicator.vertical: ScrollIndicator { }

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        InputModeManager.setNavigationMode("keyboard")
                                        InputModeManager.hideCursor(true)
                                    }
                                }

                                Keys.onReturnPressed: {
                                    autoplayCountdownCombo.currentIndex = currentIndex
                                    autoplayCountdownCombo.popup.close()
                                }
                                Keys.onEnterPressed: {
                                    autoplayCountdownCombo.currentIndex = currentIndex
                                    autoplayCountdownCombo.popup.close()
                                }
                                Keys.onEscapePressed: autoplayCountdownCombo.popup.close()
                            }

                            background: Rectangle {
                                color: Theme.cardBackground
                                border.color: Theme.focusBorder
                                border.width: 1
                                radius: Theme.radiusSmall
                            }
                        }
                    }
                }

                // ── Group 2: Completion threshold ──

                SettingsGroupDivider { Layout.fillWidth: true }

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

                    KeyNavigation.up: autoplayCountdownCombo.enabled ? autoplayCountdownCombo : autoplaySwitch
                    KeyNavigation.down: audioDelaySpinBox

                    onSliderValueChanged: function(newValue) {
                        ConfigManager.playbackCompletionThreshold = newValue
                    }
                }

                // ── Group 3: Audio delay ──

                SettingsGroupDivider { Layout.fillWidth: true }

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

                    KeyNavigation.up: thresholdSlider
                    KeyNavigation.down: defaultAudioTrackCombo

                    onSpinBoxValueChanged: function(newValue) {
                        ConfigManager.audioDelay = newValue
                    }
                }

                // ── Group 4: Default audio/subtitle fallback tracks ──

                SettingsGroupDivider { Layout.fillWidth: true }

                RowLayout {
                    id: defaultAudioTrackRow
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        spacing: Math.round(4 * Theme.layoutScale)
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Audio Language")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Fallback used when no season or movie audio preference is saved")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    ComboBox {
                        id: defaultAudioTrackCombo
                        readonly property var options: root.defaultTrackOptions(false)
                        model: options
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.optionIndexForValue(options, ConfigManager.defaultAudioTrackSelection)
                        Layout.preferredWidth: Math.round(260 * Theme.layoutScale)
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
                                audioDelaySpinBox.forceActiveFocus()
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

                        background: Rectangle {
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: Theme.inputBackground
                            border.color: defaultAudioTrackCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: defaultAudioTrackCombo.activeFocus ? 2 : 1
                        }

                        contentItem: Text {
                            text: defaultAudioTrackCombo.displayText
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                            elide: Text.ElideRight
                        }

                        delegate: ItemDelegate {
                            width: defaultAudioTrackCombo.width
                            contentItem: Text {
                                text: modelData.label
                                color: highlighted ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                color: highlighted ? Theme.buttonPrimaryBackground : "transparent"
                                radius: Theme.radiusSmall
                            }
                            highlighted: ListView.isCurrentItem || defaultAudioTrackCombo.highlightedIndex === index
                        }

                        popup: Popup {
                            y: defaultAudioTrackCombo.height + 5
                            width: defaultAudioTrackCombo.width
                            implicitHeight: Math.min(contentItem.implicitHeight, Math.round(360 * Theme.layoutScale))
                            padding: 1

                            onOpened: {
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                                defaultAudioTrackList.currentIndex = defaultAudioTrackCombo.highlightedIndex >= 0
                                    ? defaultAudioTrackCombo.highlightedIndex
                                    : defaultAudioTrackCombo.currentIndex
                                defaultAudioTrackList.forceActiveFocus()
                            }
                            onClosed: {
                                Qt.callLater(function() {
                                    defaultAudioTrackCombo.forceActiveFocus()
                                })
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }

                            contentItem: ListView {
                                id: defaultAudioTrackList
                                clip: true
                                implicitHeight: contentHeight
                                model: defaultAudioTrackCombo.popup.visible ? defaultAudioTrackCombo.delegateModel : null
                                currentIndex: defaultAudioTrackCombo.highlightedIndex >= 0
                                    ? defaultAudioTrackCombo.highlightedIndex
                                    : defaultAudioTrackCombo.currentIndex

                                ScrollIndicator.vertical: ScrollIndicator { }

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        InputModeManager.setNavigationMode("keyboard")
                                        InputModeManager.hideCursor(true)
                                    }
                                }

                                Keys.onReturnPressed: function(event) {
                                    defaultAudioTrackCombo.currentIndex = currentIndex
                                    defaultAudioTrackCombo.popup.close()
                                    event.accepted = true
                                }
                                Keys.onEnterPressed: function(event) {
                                    defaultAudioTrackCombo.currentIndex = currentIndex
                                    defaultAudioTrackCombo.popup.close()
                                    event.accepted = true
                                }
                                Keys.onEscapePressed: function(event) {
                                    defaultAudioTrackCombo.popup.close()
                                    event.accepted = true
                                }
                            }

                            background: Rectangle {
                                color: Theme.cardBackground
                                border.color: Theme.focusBorder
                                border.width: 1
                                radius: Theme.radiusSmall
                            }
                        }
                    }
                }

                RowLayout {
                    id: defaultSubtitleTrackRow
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        spacing: Math.round(4 * Theme.layoutScale)
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Subtitle Language")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Fallback used when no season or movie subtitle preference is saved")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    ComboBox {
                        id: defaultSubtitleTrackCombo
                        readonly property var options: root.defaultTrackOptions(true)
                        model: options
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.optionIndexForValue(options, ConfigManager.defaultSubtitleTrackSelection)
                        Layout.preferredWidth: Math.round(260 * Theme.layoutScale)
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

                        background: Rectangle {
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: Theme.inputBackground
                            border.color: defaultSubtitleTrackCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: defaultSubtitleTrackCombo.activeFocus ? 2 : 1
                        }

                        contentItem: Text {
                            text: defaultSubtitleTrackCombo.displayText
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                            elide: Text.ElideRight
                        }

                        delegate: ItemDelegate {
                            width: defaultSubtitleTrackCombo.width
                            contentItem: Text {
                                text: modelData.label
                                color: highlighted ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                color: highlighted ? Theme.buttonPrimaryBackground : "transparent"
                                radius: Theme.radiusSmall
                            }
                            highlighted: ListView.isCurrentItem || defaultSubtitleTrackCombo.highlightedIndex === index
                        }

                        popup: Popup {
                            y: defaultSubtitleTrackCombo.height + 5
                            width: defaultSubtitleTrackCombo.width
                            implicitHeight: Math.min(contentItem.implicitHeight, Math.round(360 * Theme.layoutScale))
                            padding: 1

                            onOpened: {
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                                defaultSubtitleTrackList.currentIndex = defaultSubtitleTrackCombo.highlightedIndex >= 0
                                    ? defaultSubtitleTrackCombo.highlightedIndex
                                    : defaultSubtitleTrackCombo.currentIndex
                                defaultSubtitleTrackList.forceActiveFocus()
                            }
                            onClosed: {
                                Qt.callLater(function() {
                                    defaultSubtitleTrackCombo.forceActiveFocus()
                                })
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }

                            contentItem: ListView {
                                id: defaultSubtitleTrackList
                                clip: true
                                implicitHeight: contentHeight
                                model: defaultSubtitleTrackCombo.popup.visible ? defaultSubtitleTrackCombo.delegateModel : null
                                currentIndex: defaultSubtitleTrackCombo.highlightedIndex >= 0
                                    ? defaultSubtitleTrackCombo.highlightedIndex
                                    : defaultSubtitleTrackCombo.currentIndex

                                ScrollIndicator.vertical: ScrollIndicator { }

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        InputModeManager.setNavigationMode("keyboard")
                                        InputModeManager.hideCursor(true)
                                    }
                                }

                                Keys.onReturnPressed: function(event) {
                                    defaultSubtitleTrackCombo.currentIndex = currentIndex
                                    defaultSubtitleTrackCombo.popup.close()
                                    event.accepted = true
                                }
                                Keys.onEnterPressed: function(event) {
                                    defaultSubtitleTrackCombo.currentIndex = currentIndex
                                    defaultSubtitleTrackCombo.popup.close()
                                    event.accepted = true
                                }
                                Keys.onEscapePressed: function(event) {
                                    defaultSubtitleTrackCombo.popup.close()
                                    event.accepted = true
                                }
                            }

                            background: Rectangle {
                                color: Theme.cardBackground
                                border.color: Theme.focusBorder
                                border.width: 1
                                radius: Theme.radiusSmall
                            }
                        }
                    }
                }

                // ── Group 5: Skip popup + auto-skip toggles ──

                SettingsGroupDivider { Layout.fillWidth: true }

                SettingsSliderRow {
                    id: skipPopupDurationSlider
                    label: qsTr("Skip Intro/Credits Popup Duration")
                    description: qsTr("How long the playback skip popup stays visible (0 disables popup)")
                    value: ConfigManager.skipButtonAutoHideSeconds
                    from: 0
                    to: 15
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
                    KeyNavigation.down: uiSoundsToggle

                    onToggled: function(value) {
                        ConfigManager.autoSkipOutro = value
                    }
                }

                // ── Group 5: UI sounds toggle + volume ──

                SettingsGroupDivider { Layout.fillWidth: true }

                SettingsToggleRow {
                    id: uiSoundsToggle
                    label: qsTr("UI Sounds")
                    description: qsTr("Play a sound on navigation, select, and back actions")
                    checked: ConfigManager.uiSoundsEnabled
                    Layout.fillWidth: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }

                    KeyNavigation.up: autoSkipOutroToggle
                    KeyNavigation.down: uiSoundsVolumeCombo.enabled ? uiSoundsVolumeCombo : themeSongVolumeCombo

                    onToggled: function(value) {
                        ConfigManager.uiSoundsEnabled = value
                    }
                }

                RowLayout {
                    id: uiSoundsVolumeRow
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        spacing: Math.round(4 * Theme.layoutScale)
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("UI Sound Volume")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: uiSoundsVolumeCombo.enabled ? Theme.textPrimary : Theme.textDisabled
                        }

                        Text {
                            text: qsTr("Click feedback for navigation and selection")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: uiSoundsVolumeCombo.enabled ? Theme.textSecondary : Theme.textDisabled
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    ComboBox {
                        id: uiSoundsVolumeCombo
                        model: [qsTr("Off"), qsTr("Very Low"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                        currentIndex: 0
                        Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
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

                        background: Rectangle {
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: Theme.inputBackground
                            border.color: uiSoundsVolumeCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: uiSoundsVolumeCombo.activeFocus ? 2 : 1
                            opacity: uiSoundsVolumeCombo.enabled ? 1.0 : 0.5
                        }

                        contentItem: Text {
                            text: uiSoundsVolumeCombo.displayText
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: uiSoundsVolumeCombo.enabled ? Theme.textPrimary : Theme.textDisabled
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                        }

                        delegate: ItemDelegate {
                            width: uiSoundsVolumeCombo.width
                            enabled: uiSoundsVolumeCombo.enabled
                            contentItem: Text {
                                text: modelData
                                color: highlighted ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: highlighted ? Theme.buttonPrimaryBackground : "transparent"
                                radius: Theme.radiusSmall
                            }
                            highlighted: ListView.isCurrentItem || uiSoundsVolumeCombo.highlightedIndex === index
                        }

                        popup: Popup {
                            y: uiSoundsVolumeCombo.height + 5
                            width: uiSoundsVolumeCombo.width
                            implicitHeight: contentItem.implicitHeight
                            padding: 1

                            onOpened: {
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                                uiSoundsVolumeList.currentIndex = uiSoundsVolumeCombo.highlightedIndex >= 0
                                    ? uiSoundsVolumeCombo.highlightedIndex
                                    : uiSoundsVolumeCombo.currentIndex
                                uiSoundsVolumeList.forceActiveFocus()
                            }
                            onClosed: {
                                uiSoundsVolumeCombo.forceActiveFocus()
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }

                            contentItem: ListView {
                                id: uiSoundsVolumeList
                                clip: true
                                implicitHeight: contentHeight
                                model: uiSoundsVolumeCombo.popup.visible ? uiSoundsVolumeCombo.delegateModel : null
                                currentIndex: uiSoundsVolumeCombo.highlightedIndex >= 0
                                    ? uiSoundsVolumeCombo.highlightedIndex
                                    : uiSoundsVolumeCombo.currentIndex

                                ScrollIndicator.vertical: ScrollIndicator { }

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        InputModeManager.setNavigationMode("keyboard")
                                        InputModeManager.hideCursor(true)
                                    }
                                }

                                Keys.onReturnPressed: {
                                    if (currentIndex !== ConfigManager.uiSoundsVolume) {
                                        ConfigManager.uiSoundsVolume = currentIndex
                                    }
                                    uiSoundsVolumeCombo.popup.close()
                                }
                                Keys.onEnterPressed: {
                                    if (currentIndex !== ConfigManager.uiSoundsVolume) {
                                        ConfigManager.uiSoundsVolume = currentIndex
                                    }
                                    uiSoundsVolumeCombo.popup.close()
                                }
                                Keys.onEscapePressed: uiSoundsVolumeCombo.popup.close()
                            }

                            background: Rectangle {
                                color: Theme.cardBackground
                                border.color: Theme.focusBorder
                                border.width: 1
                                radius: Theme.radiusSmall
                            }
                        }
                    }
                }

                // ── Group 6: Theme song volume + loop ──

                SettingsGroupDivider { Layout.fillWidth: true }

                RowLayout {
                    id: themeSongVolumeRow
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        spacing: Math.round(4 * Theme.layoutScale)
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Theme Song Volume")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Play series theme music when browsing")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    ComboBox {
                        id: themeSongVolumeCombo
                        model: [qsTr("Off"), qsTr("Very Low"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                        currentIndex: 0
                        Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
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

                        background: Rectangle {
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: Theme.inputBackground
                            border.color: themeSongVolumeCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: themeSongVolumeCombo.activeFocus ? 2 : 1
                        }

                        contentItem: Text {
                            text: themeSongVolumeCombo.displayText
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                        }

                        delegate: ItemDelegate {
                            width: themeSongVolumeCombo.width
                            contentItem: Text {
                                text: modelData
                                color: highlighted ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: highlighted ? Theme.buttonPrimaryBackground : "transparent"
                                radius: Theme.radiusSmall
                            }
                            highlighted: ListView.isCurrentItem || themeSongVolumeCombo.highlightedIndex === index
                        }

                        popup: Popup {
                            y: themeSongVolumeCombo.height + 5
                            width: themeSongVolumeCombo.width
                            implicitHeight: contentItem.implicitHeight
                            padding: 1

                            onOpened: {
                                InputModeManager.setNavigationMode("keyboard")
                                InputModeManager.hideCursor(true)
                                themeSongVolumeList.currentIndex = themeSongVolumeCombo.highlightedIndex >= 0
                                    ? themeSongVolumeCombo.highlightedIndex
                                    : themeSongVolumeCombo.currentIndex
                                themeSongVolumeList.forceActiveFocus()
                            }
                            onClosed: {
                                themeSongVolumeCombo.forceActiveFocus()
                                InputModeManager.setNavigationMode("pointer")
                                InputModeManager.hideCursor(false)
                            }

                            contentItem: ListView {
                                id: themeSongVolumeList
                                clip: true
                                implicitHeight: contentHeight
                                model: themeSongVolumeCombo.popup.visible ? themeSongVolumeCombo.delegateModel : null
                                currentIndex: themeSongVolumeCombo.highlightedIndex >= 0
                                    ? themeSongVolumeCombo.highlightedIndex
                                    : themeSongVolumeCombo.currentIndex

                                ScrollIndicator.vertical: ScrollIndicator { }

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        InputModeManager.setNavigationMode("keyboard")
                                        InputModeManager.hideCursor(true)
                                    }
                                }

                                Keys.onReturnPressed: {
                                    if (currentIndex !== ConfigManager.themeSongVolume) {
                                        ConfigManager.themeSongVolume = currentIndex
                                    }
                                    themeSongVolumeCombo.popup.close()
                                }
                                Keys.onEnterPressed: {
                                    if (currentIndex !== ConfigManager.themeSongVolume) {
                                        ConfigManager.themeSongVolume = currentIndex
                                    }
                                    themeSongVolumeCombo.popup.close()
                                }
                                Keys.onEscapePressed: themeSongVolumeCombo.popup.close()
                            }

                            background: Rectangle {
                                color: Theme.cardBackground
                                border.color: Theme.focusBorder
                                border.width: 1
                                radius: Theme.radiusSmall
                            }
                        }
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

                // ── Group 7: Playback cache size ──

                SettingsGroupDivider { Layout.fillWidth: true }

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

                    onSpinBoxValueChanged: function(newValue) {
                        ConfigManager.playbackCacheSizeMB = newValue
                    }
                }
            }
        }

        WheelStepScroller {
            anchors.fill: flickable
            target: flickable
            stepPx: Math.round(88 * Theme.layoutScale)
        }
    }
}
