import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()

    readonly property Item preferredEntryItem: screensaverEnabledRow
    property Item _lastFocusedItem: null

    function enterFromRail() {
        var target = (_lastFocusedItem && _lastFocusedItem.visible) ? _lastFocusedItem : preferredEntryItem
        if (target) target.forceActiveFocus()
    }

    function restoreFocus() {
        enterFromRail()
    }

    function nextThemeVariantControl() {
        if (flavorRow.visible) return flavorCombo
        if (colorSchemeRow.visible) return colorSchemeCombo
        return fullscreenToggle
    }

    function previousThemeVariantControl() {
        if (colorSchemeRow.visible) return colorSchemeCombo
        if (flavorRow.visible) return flavorCombo
        return themeCombo
    }

    function previousColorSchemeControl() {
        return flavorRow.visible ? flavorCombo : themeCombo
    }

    function screensaverModeIndex() {
        var values = ["libraryBackdrops", "bouncingLogo", "black"]
        return Math.max(0, values.indexOf(ConfigManager.screensaverMode))
    }

    function syncThemeVariants(themeName, useDefaults) {
        var flavor = useDefaults ? Theme.defaultFlavorForTheme(themeName)
                                 : Theme.validFlavorIdForTheme(themeName, ConfigManager.themeFlavor || "")
        if (flavor !== (ConfigManager.themeFlavor || ""))
            ConfigManager.themeFlavor = flavor

        var colorScheme = useDefaults ? Theme.defaultColorSchemeForTheme(themeName)
                                      : Theme.validColorSchemeIdForTheme(themeName, ConfigManager.themeColorScheme || "")
        if (colorScheme !== (ConfigManager.themeColorScheme || ""))
            ConfigManager.themeColorScheme = colorScheme
    }

    function restoreFlavorIndex() {
        flavorCombo.currentIndex = Math.max(0, flavorCombo.model.indexOf(Theme.currentFlavorLabel))
    }

    function restoreColorSchemeIndex() {
        colorSchemeCombo.currentIndex = Math.max(0, colorSchemeCombo.model.indexOf(Theme.currentColorSchemeLabel))
    }

    function applyCurrentFlavor() {
        if (!flavorCombo.currentText)
            return
        var flavorId = Theme.optionIdForLabel(Theme.themeDefinition(Theme.currentTheme).flavors || [], flavorCombo.currentText)
        if (flavorId !== ConfigManager.themeFlavor)
            ConfigManager.themeFlavor = flavorId
    }

    function applyCurrentColorScheme() {
        if (!colorSchemeCombo.currentText)
            return
        var colorSchemeId = Theme.optionIdForLabel(Theme.themeDefinition(Theme.currentTheme).colorSchemes || [], colorSchemeCombo.currentText)
        if (colorSchemeId !== ConfigManager.themeColorScheme)
            ConfigManager.themeColorScheme = colorSchemeId
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
            text: qsTr("Display")
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

                // --- Screensaver ---
                Text {
                    text: qsTr("Screensaver")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }

                SettingsToggleRow {
                    id: screensaverEnabledRow
                    label: qsTr("Enable Screensaver")
                    description: qsTr("Start an OLED-safe overlay after the app is idle.")
                    checked: ConfigManager.screensaverEnabled
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onToggled: function(value) { ConfigManager.screensaverEnabled = value }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.down: screensaverModeCombo
                }

                SettingsComboBox {
                    id: screensaverModeCombo
                    Layout.preferredWidth: Math.round(300 * Theme.layoutScale)
                    enabled: ConfigManager.screensaverEnabled
                    model: [qsTr("Library Backdrops"), qsTr("Bouncing Logo"), qsTr("Black Screen")]

                    property bool _syncingFromConfig: false
                    function modeValues() {
                        return ["libraryBackdrops", "bouncingLogo", "black"]
                    }
                    function applyModeAt(index) {
                        if (_syncingFromConfig || index < 0) return
                        var values = modeValues()
                        if (index >= values.length) return
                        ConfigManager.screensaverMode = values[index]
                    }
                    function syncFromConfig() {
                        _syncingFromConfig = true
                        currentIndex = root.screensaverModeIndex()
                        _syncingFromConfig = false
                    }

                    Component.onCompleted: syncFromConfig()
                    Connections {
                        target: ConfigManager
                        function onScreensaverModeChanged() { screensaverModeCombo.syncFromConfig() }
                    }
                    onSelectionAccepted: function(index) { applyModeAt(index) }
                    onActivated: function(index) { applyModeAt(index) }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root._lastFocusedItem = this
                            flickable.ensureFocusVisible(this)
                        }
                    }
                    KeyNavigation.up: screensaverEnabledRow
                    KeyNavigation.down: screensaverTimeoutRow
                }

                SettingsSpinBoxRow {
                    id: screensaverTimeoutRow
                    label: qsTr("Screensaver timeout")
                    description: qsTr("Minutes of inactivity before the screensaver starts.")
                    from: 1
                    to: 1440
                    stepSize: 1
                    unit: "min"
                    value: Math.max(1, Math.round(ConfigManager.screensaverTimeoutSeconds / 60))
                    enabled: ConfigManager.screensaverEnabled
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onSpinBoxValueChanged: ConfigManager.screensaverTimeoutSeconds = Math.max(1, newValue) * 60
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: screensaverModeCombo
                    KeyNavigation.down: themeCombo
                }

                SettingsGroupDivider { Layout.fillWidth: true }

                // --- Theme ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        spacing: Math.round(4 * Theme.layoutScale)
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Theme")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Application color scheme")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    SettingsComboBox {
                        id: themeCombo
                        focusPolicy: Qt.StrongFocus
                        model: Theme.themeNames
                        currentIndex: Math.max(0, model.indexOf(ConfigManager.theme || "Jellyfin"))
                        Layout.preferredWidth: Math.round(240 * Theme.layoutScale)

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        onCurrentTextChanged: {
                            if (!currentText)
                                return
                            var themeChanged = ConfigManager.theme !== currentText
                            if (themeChanged)
                                ConfigManager.theme = currentText
                            root.syncThemeVariants(currentText, themeChanged)
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                screensaverTimeoutRow.forceActiveFocus()
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                root.nextThemeVariantControl().forceActiveFocus()
                            }
                        }
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()

                    }
                }

                RowLayout {
                    id: flavorRow
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    visible: Theme.flavorNames.length > 0

                    ColumnLayout {
                        spacing: Math.round(4 * Theme.layoutScale)
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Flavor")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Theme palette variant")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    SettingsComboBox {
                        id: flavorCombo
                        focusPolicy: Qt.StrongFocus
                        model: Theme.flavorNames
                        Component.onCompleted: root.restoreFlavorIndex()
                        onModelChanged: Qt.callLater(root.restoreFlavorIndex)
                        onActivated: root.applyCurrentFlavor()
                        onSelectionAccepted: root.applyCurrentFlavor()
                        Layout.preferredWidth: Math.round(240 * Theme.layoutScale)

                        Connections {
                            target: Theme
                            function onCurrentFlavorLabelChanged() {
                                Qt.callLater(root.restoreFlavorIndex)
                            }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible)
                                themeCombo.forceActiveFocus()
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                if (colorSchemeRow.visible)
                                    colorSchemeCombo.forceActiveFocus()
                                else
                                    fullscreenToggle.forceActiveFocus()
                            }
                        }
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()

                    }
                }

                RowLayout {
                    id: colorSchemeRow
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    visible: Theme.colorSchemeNames.length > 0

                    ColumnLayout {
                        spacing: Math.round(4 * Theme.layoutScale)
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Color Scheme")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Theme accent color")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    SettingsComboBox {
                        id: colorSchemeCombo
                        focusPolicy: Qt.StrongFocus
                        model: Theme.colorSchemeNames
                        Component.onCompleted: root.restoreColorSchemeIndex()
                        onModelChanged: Qt.callLater(root.restoreColorSchemeIndex)
                        onActivated: root.applyCurrentColorScheme()
                        onSelectionAccepted: root.applyCurrentColorScheme()
                        Layout.preferredWidth: Math.round(240 * Theme.layoutScale)

                        Connections {
                            target: Theme
                            function onCurrentColorSchemeLabelChanged() {
                                Qt.callLater(root.restoreColorSchemeIndex)
                            }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible)
                                root.previousColorSchemeControl().forceActiveFocus()
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible)
                                fullscreenToggle.forceActiveFocus()
                        }
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()

                    }
                }

                SettingsGroupDivider { Layout.fillWidth: true }

                // --- Launch in Fullscreen ---
                SettingsToggleRow {
                    id: fullscreenToggle
                    label: qsTr("Launch in Fullscreen")
                    description: qsTr("Start Bloom in fullscreen on launch. Disable this to open in a window.")
                    checked: ConfigManager.launchInFullscreen
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onToggled: function(value) { ConfigManager.launchInFullscreen = value }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: root.previousThemeVariantControl()
                    KeyNavigation.down: backdropSlider
                }

                SettingsGroupDivider { Layout.fillWidth: true }

                // --- Backdrop Rotation ---
                SettingsSliderRow {
                    id: backdropSlider
                    label: qsTr("Backdrop Rotation")
                    description: qsTr("How often the home screen backdrop changes")
                    value: ConfigManager.backdropRotationInterval / 1000
                    from: 10
                    to: 120
                    stepSize: 5
                    unit: "s"
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onSliderValueChanged: function(newValue) { ConfigManager.backdropRotationInterval = newValue * 1000 }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: fullscreenToggle
                    KeyNavigation.down: dpiScaleSlider
                }

                SettingsGroupDivider { Layout.fillWidth: true }

                // --- Content Scale Override ---
                SettingsSliderRow {
                    id: dpiScaleSlider

                    property real overrideVal: ConfigManager.manualDpiScaleOverride
                    property real effectiveVal: Theme.layoutScale
                    property real autoScaleVal: effectiveVal / (overrideVal || 1.0)

                    label: qsTr("Content Scale Override")
                    description: qsTr("Auto-detected: %1x  |  Override: %2x  |  Effective: %3x")
                        .arg(autoScaleVal.toFixed(2))
                        .arg(overrideVal.toFixed(2))
                        .arg(effectiveVal.toFixed(2))
                    value: ConfigManager.manualDpiScaleOverride
                    from: 0.5
                    to: 2.0
                    stepSize: 0.1
                    unit: "x"
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onSliderValueChanged: function(newValue) { ConfigManager.manualDpiScaleOverride = newValue }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.up: backdropSlider
                }
            }
        }
    }
}
