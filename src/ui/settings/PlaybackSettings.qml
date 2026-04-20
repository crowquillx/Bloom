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

    onActiveFocusItemChanged: {
        let item = activeFocusItem
        if (item && item !== root && item.visible) {
            // Walk up to find the direct child control of contentColumn
            let candidate = item
            while (candidate && candidate.parent !== contentColumn) {
                candidate = candidate.parent
            }
            if (candidate && candidate.parent === contentColumn) {
                _lastFocusedItem = candidate
            }
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
        anchors.margins: Theme.spacingLarge
        spacing: 0

        Text {
            text: qsTr("Playback")
            font.pixelSize: Theme.fontSizeHeader
            font.family: Theme.fontPrimary
            font.weight: Font.Bold
            color: Theme.textPrimary
            Layout.bottomMargin: Theme.spacingMedium
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderLight
            Layout.bottomMargin: Theme.spacingMedium
        }

        Flickable {
            id: flickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: contentColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            function ensureFocusVisible(item) {
                if (!item) return
                var mapped = item.mapToItem(contentColumn, 0, 0)
                var itemY = mapped.y
                var itemHeight = item.height
                var viewTop = contentY
                var viewBottom = contentY + height
                var padding = 50

                if (itemY < viewTop + padding) {
                    contentY = Math.max(0, itemY - padding)
                } else if (itemY + itemHeight > viewBottom - padding) {
                    contentY = Math.min(contentHeight - height, itemY + itemHeight - height + padding)
                }
            }

            ColumnLayout {
                id: contentColumn
                width: flickable.width
                spacing: Theme.spacingLarge

                // ── Group 1: Autoplay ──

                SettingsToggleRow {
                    id: autoplaySwitch
                    label: qsTr("Autoplay Next Episode")
                    description: qsTr("Automatically play the next episode when the current one ends")
                    checked: ConfigManager.autoplayNextEpisode
                    Layout.fillWidth: true
                    focus: true
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }

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

                    KeyNavigation.up: thresholdSlider
                    KeyNavigation.down: skipPopupDurationSlider

                    onSpinBoxValueChanged: function(newValue) {
                        ConfigManager.audioDelay = newValue
                    }
                }

                // ── Group 4: Skip popup + auto-skip toggles ──

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

                    KeyNavigation.up: audioDelaySpinBox
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

                    KeyNavigation.up: autoSkipOutroToggle
                    KeyNavigation.down: uiSoundsVolumeCombo

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
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Click feedback for navigation and selection")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    ComboBox {
                        id: uiSoundsVolumeCombo
                        model: [qsTr("Off"), qsTr("Very Low"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                        currentIndex: ConfigManager.uiSoundsVolume
                        Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
                        focusPolicy: Qt.StrongFocus
                        enabled: ConfigManager.uiSoundsEnabled

                        onActiveFocusChanged: {
                            if (activeFocus) flickable.ensureFocusVisible(this)
                        }

                        onCurrentIndexChanged: {
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
                                uiSoundsVolumeList.currentIndex = uiSoundsVolumeCombo.highlightedIndex >= 0
                                    ? uiSoundsVolumeCombo.highlightedIndex
                                    : uiSoundsVolumeCombo.currentIndex
                                uiSoundsVolumeList.forceActiveFocus()
                            }
                            onClosed: uiSoundsVolumeCombo.forceActiveFocus()

                            contentItem: ListView {
                                id: uiSoundsVolumeList
                                clip: true
                                implicitHeight: contentHeight
                                model: uiSoundsVolumeCombo.popup.visible ? uiSoundsVolumeCombo.delegateModel : null
                                currentIndex: uiSoundsVolumeCombo.highlightedIndex >= 0
                                    ? uiSoundsVolumeCombo.highlightedIndex
                                    : uiSoundsVolumeCombo.currentIndex

                                ScrollIndicator.vertical: ScrollIndicator { }

                                Keys.onReturnPressed: {
                                    uiSoundsVolumeCombo.currentIndex = currentIndex
                                    uiSoundsVolumeCombo.popup.close()
                                }
                                Keys.onEnterPressed: {
                                    uiSoundsVolumeCombo.currentIndex = currentIndex
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
                        currentIndex: ConfigManager.themeSongVolume
                        Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
                        focusPolicy: Qt.StrongFocus

                        onActiveFocusChanged: {
                            if (activeFocus) flickable.ensureFocusVisible(this)
                        }

                        onCurrentIndexChanged: {
                            if (currentIndex !== ConfigManager.themeSongVolume) {
                                ConfigManager.themeSongVolume = currentIndex
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                uiSoundsVolumeCombo.forceActiveFocus()
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
                                themeSongVolumeList.currentIndex = themeSongVolumeCombo.highlightedIndex >= 0
                                    ? themeSongVolumeCombo.highlightedIndex
                                    : themeSongVolumeCombo.currentIndex
                                themeSongVolumeList.forceActiveFocus()
                            }
                            onClosed: themeSongVolumeCombo.forceActiveFocus()

                            contentItem: ListView {
                                id: themeSongVolumeList
                                clip: true
                                implicitHeight: contentHeight
                                model: themeSongVolumeCombo.popup.visible ? themeSongVolumeCombo.delegateModel : null
                                currentIndex: themeSongVolumeCombo.highlightedIndex >= 0
                                    ? themeSongVolumeCombo.highlightedIndex
                                    : themeSongVolumeCombo.currentIndex

                                ScrollIndicator.vertical: ScrollIndicator { }

                                Keys.onReturnPressed: {
                                    themeSongVolumeCombo.currentIndex = currentIndex
                                    themeSongVolumeCombo.popup.close()
                                }
                                Keys.onEnterPressed: {
                                    themeSongVolumeCombo.currentIndex = currentIndex
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

                    KeyNavigation.up: themeSongVolumeCombo

                    onToggled: function(value) {
                        ConfigManager.themeSongLoop = value
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
