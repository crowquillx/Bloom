import QtQuick
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()
    readonly property bool scrolled: flickable.scrolled
    signal signOutRequested()
    signal sessionsRequested()

    readonly property Item preferredEntryItem: autoUpdateCheckToggle
    property Item _lastFocusedItem: null

    property string appVersion: ""
    property string appBuildChannel: ""
    property string appBuildId: ""
    property string qtVersion: ""

    function enterFromRail() {
        var target = (_lastFocusedItem && _lastFocusedItem.visible) ? _lastFocusedItem : preferredEntryItem
        if (target) target.forceActiveFocus()
    }

    function restoreFocus() {
        enterFromRail()
    }

    function formatUpdateCheckTimestamp(timestamp) {
        if (!timestamp || timestamp.length === 0) return qsTr("Never")
        var parsed = new Date(timestamp)
        if (isNaN(parsed.getTime())) return qsTr("Never")
        return parsed.toLocaleString(Qt.locale())
    }

    function visibleUpdateRowButtons() {
        var buttons = []
        if (downloadInstallBtn.visible) buttons.push(downloadInstallBtn)
        if (openInstallerBtn.visible) buttons.push(openInstallerBtn)
        if (openPortableBtn.visible) buttons.push(openPortableBtn)
        return buttons
    }

    function logLevelIndexForValue(value) {
        if (value === "debug") return 1
        if (value === "quiet") return 2
        return 0
    }

    function logLevelValueForIndex(index) {
        if (index === 1) return "debug"
        if (index === 2) return "quiet"
        return "info"
    }

    function focusLastUpdatesControl() {
        if (openDownloadPageBtn.visible) {
            openDownloadPageBtn.forceActiveFocus()
            return
        }
        var rowButtons = visibleUpdateRowButtons()
        if (rowButtons.length > 0) {
            rowButtons[rowButtons.length - 1].forceActiveFocus()
            return
        }
        if (checkUpdatesBtn.enabled) {
            checkUpdatesBtn.forceActiveFocus()
            return
        }
        logLevelCombo.forceActiveFocus()
    }

    function focusAdjacentUpdateButton(currentButton, step) {
        var buttons = [checkUpdatesBtn].concat(visibleUpdateRowButtons())
        if (openDownloadPageBtn.visible) buttons.push(openDownloadPageBtn)
        var index = buttons.indexOf(currentButton)
        var nextIndex = index + step
        if (index >= 0 && nextIndex >= 0 && nextIndex < buttons.length) {
            buttons[nextIndex].forceActiveFocus()
            return true
        }
        return false
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
                    text: qsTr("About Bloom")
                    Layout.fillWidth: true
                    Layout.topMargin: 0
                }

                SettingsInfoRow {
                    label: qsTr("Version")
                    value: root.appVersion
                }

                SettingsInfoRow {
                    label: qsTr("Qt Version")
                    value: root.qtVersion
                }

                Text {
                    text: qsTr("Bloom is a media client for Jellyfin and Silo servers designed for the 10-foot TV experience.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingSmall
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Updates")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsToggleRow {
                    id: autoUpdateCheckToggle
                    label: qsTr("Automatically Check for Updates")
                    description: qsTr("Check for Bloom updates at app startup.")
                    checked: ConfigManager.autoUpdateCheckEnabled
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    onToggled: function(value) { ConfigManager.autoUpdateCheckEnabled = value }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                    KeyNavigation.down: updateChannelCombo
                }

                // Update Channel
                SettingsRow {
                    label: qsTr("Update Channel")
                    description: qsTr("Choose which release channel to follow for updates.")
                    highlighted: updateChannelCombo.activeFocus || updateChannelCombo.popup.visible
                    focusTarget: updateChannelCombo
                    onActivated: updateChannelCombo.popup.open()

                    SettingsComboBox {
                        id: updateChannelCombo
                        focusPolicy: Qt.StrongFocus
                        model: ["Stable", "Development"]

                        property bool initialized: false
                        property bool updatingSelection: false

                        Component.onCompleted: {
                            currentIndex = ConfigManager.updateChannel === "dev" ? 1 : 0
                            initialized = true
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (!initialized || updatingSelection) return
                            var channel = currentIndex === 1 ? "dev" : "stable"
                            if (channel !== ConfigManager.updateChannel) {
                                UpdateService.setChannel(channel)
                            }
                        }

                        Connections {
                            target: ConfigManager
                            function onUpdateChannelChanged() {
                                updateChannelCombo.updatingSelection = true
                                updateChannelCombo.currentIndex = ConfigManager.updateChannel === "dev" ? 1 : 0
                                updateChannelCombo.updatingSelection = false
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                autoUpdateCheckToggle.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                if (checkUpdatesBtn.enabled) checkUpdatesBtn.forceActiveFocus()
                                else if (openDownloadPageBtn.visible) openDownloadPageBtn.forceActiveFocus()
                                else logLevelCombo.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()
                    }
                }

                // Button Row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingSmall
                    Layout.bottomMargin: Theme.spacingSmall
                    spacing: Theme.controlSpacing

                    ActionButton {
                        id: checkUpdatesBtn
                        variant: "secondary"
                        focusPolicy: Qt.StrongFocus
                        text: UpdateService.checking ? qsTr("Checking...") : qsTr("Check for Updates")
                        enabled: !UpdateService.checking && !UpdateService.downloadInProgress && !UpdateService.installerLaunched
                        onClicked: UpdateService.checkForUpdates(true)
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }
                        Keys.onUpPressed: function(event) {
                            updateChannelCombo.forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onDownPressed: function(event) {
                            if (openDownloadPageBtn.visible) {
                                openDownloadPageBtn.forceActiveFocus()
                            } else {
                                logLevelCombo.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                        Keys.onLeftPressed: function(event) {
                            event.accepted = true
                        }
                        Keys.onRightPressed: function(event) {
                            root.focusAdjacentUpdateButton(checkUpdatesBtn, 1)
                            event.accepted = true
                        }
                    }

                    ActionButton {
                        id: downloadInstallBtn
                        variant: "primary"
                        focusPolicy: Qt.StrongFocus
                        visible: UpdateService.updateAvailable && UpdateService.applySupported
                        text: qsTr("Download and Install")
                        onClicked: UpdateService.downloadAndInstallUpdate()
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }
                        Keys.onUpPressed: function(event) {
                            updateChannelCombo.forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onDownPressed: function(event) {
                            if (openDownloadPageBtn.visible) {
                                openDownloadPageBtn.forceActiveFocus()
                            } else {
                                logLevelCombo.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                        Keys.onLeftPressed: function(event) {
                            root.focusAdjacentUpdateButton(downloadInstallBtn, -1)
                            event.accepted = true
                        }
                        Keys.onRightPressed: function(event) {
                            root.focusAdjacentUpdateButton(downloadInstallBtn, 1)
                            event.accepted = true
                        }
                    }

                    ActionButton {
                        id: openInstallerBtn
                        variant: "secondary"
                        focusPolicy: Qt.StrongFocus
                        visible: UpdateService.updateAvailable && !UpdateService.applySupported && !UpdateService.packageManaged && UpdateService.installerUrl.length > 0
                        text: qsTr("Open Installer")
                        onClicked: UpdateService.openInstallerAsset()
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }
                        Keys.onUpPressed: function(event) {
                            updateChannelCombo.forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onDownPressed: function(event) {
                            if (openDownloadPageBtn.visible) {
                                openDownloadPageBtn.forceActiveFocus()
                            } else {
                                logLevelCombo.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                        Keys.onLeftPressed: function(event) {
                            root.focusAdjacentUpdateButton(openInstallerBtn, -1)
                            event.accepted = true
                        }
                        Keys.onRightPressed: function(event) {
                            root.focusAdjacentUpdateButton(openInstallerBtn, 1)
                            event.accepted = true
                        }
                    }

                    ActionButton {
                        id: openPortableBtn
                        variant: "secondary"
                        focusPolicy: Qt.StrongFocus
                        visible: UpdateService.updateAvailable && !UpdateService.applySupported && !UpdateService.packageManaged && UpdateService.portableUrl.length > 0
                        text: qsTr("Open Portable ZIP")
                        onClicked: UpdateService.openPortableAsset()
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }
                        Keys.onUpPressed: function(event) {
                            updateChannelCombo.forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onDownPressed: function(event) {
                            if (openDownloadPageBtn.visible) {
                                openDownloadPageBtn.forceActiveFocus()
                            } else {
                                logLevelCombo.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                        Keys.onLeftPressed: function(event) {
                            root.focusAdjacentUpdateButton(openPortableBtn, -1)
                            event.accepted = true
                        }
                        Keys.onRightPressed: function(event) {
                            root.focusAdjacentUpdateButton(openPortableBtn, 1)
                            event.accepted = true
                        }
                    }
                }

                // Info rows
                SettingsInfoRow {
                    label: qsTr("Current Version")
                    value: root.appVersion
                }

                SettingsInfoRow {
                    label: qsTr("Current Build")
                    value: root.appBuildChannel + " / " + root.appBuildId
                }

                SettingsInfoRow {
                    label: qsTr("Selected Channel")
                    value: ConfigManager.updateChannel
                }

                SettingsInfoRow {
                    label: qsTr("Last Checked")
                    value: root.formatUpdateCheckTimestamp(ConfigManager.lastUpdateCheckAt)
                }

                SettingsInfoRow {
                    label: qsTr("Status")
                    value: UpdateService.statusMessage.length > 0 ? UpdateService.statusMessage : qsTr("No update check has been run yet.")
                }

                // Release notes box
                Rectangle {
                    Layout.fillWidth: true
                    visible: UpdateService.releaseNotes.length > 0
                    implicitHeight: releaseNotesText.implicitHeight + Theme.spacingMedium * 2
                    radius: Theme.radiusMedium
                    color: Theme.surfaceInteractiveDisabled
                    border.color: Theme.surfaceBorder
                    border.width: Theme.borderWidth

                    Text {
                        id: releaseNotesText
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMedium
                        text: UpdateService.releaseNotes
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }
                }

                // Open Download Page button
                ActionButton {
                    id: openDownloadPageBtn
                    variant: "secondary"
                    focusPolicy: Qt.StrongFocus
                    visible: UpdateService.updateAvailable
                    text: qsTr("Open Download Page")
                    onClicked: UpdateService.openUpdateDownloadPage()
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root._lastFocusedItem = this
                            flickable.ensureFocusVisible(this)
                        }
                    }
                    Keys.onUpPressed: function(event) {
                        var rowButtons = root.visibleUpdateRowButtons()
                        if (rowButtons.length > 0) {
                            rowButtons[rowButtons.length - 1].forceActiveFocus()
                        } else {
                            checkUpdatesBtn.forceActiveFocus()
                        }
                        event.accepted = true
                    }
                    Keys.onDownPressed: function(event) {
                        logLevelCombo.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onLeftPressed: function(event) {
                        var rowButtons = root.visibleUpdateRowButtons()
                        if (rowButtons.length > 0) {
                            rowButtons[rowButtons.length - 1].forceActiveFocus()
                        } else {
                            checkUpdatesBtn.forceActiveFocus()
                        }
                        event.accepted = true
                    }
                    Keys.onRightPressed: function(event) {
                        event.accepted = true
                    }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Diagnostics")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsRow {
                    label: qsTr("Log Level")
                    description: qsTr("Controls how much detail is written to the log file. Warnings and errors are always kept.")
                    highlighted: logLevelCombo.activeFocus || logLevelCombo.popup.visible
                    focusTarget: logLevelCombo
                    onActivated: logLevelCombo.popup.open()

                    SettingsComboBox {
                        id: logLevelCombo
                        focusPolicy: Qt.StrongFocus
                        model: [qsTr("Normal"), qsTr("Verbose"), qsTr("Quiet (warnings only)")]

                        property bool initialized: false
                        property bool updatingSelection: false

                        Component.onCompleted: {
                            currentIndex = root.logLevelIndexForValue(ConfigManager.logLevel)
                            initialized = true
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (!initialized || updatingSelection) return
                            var level = root.logLevelValueForIndex(currentIndex)
                            if (level !== ConfigManager.logLevel) {
                                ConfigManager.logLevel = level
                            }
                        }

                        Connections {
                            target: ConfigManager
                            function onLogLevelChanged() {
                                logLevelCombo.updatingSelection = true
                                logLevelCombo.currentIndex = root.logLevelIndexForValue(ConfigManager.logLevel)
                                logLevelCombo.updatingSelection = false
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                root.focusLastUpdatesControl()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                (activeSessionsBtn.enabled
                                    ? activeSessionsBtn : signOutBtn).forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()
                    }
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Account")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsInfoRow {
                    id: serverInfoRow
                    label: qsTr("Server")
                    value: ConfigManager.serverUrl || qsTr("Not connected")
                }

                SettingsInfoRow {
                    label: qsTr("User")
                    value: ConfigManager.username || qsTr("Unknown")
                }
                // Session management is available for both providers, but the
                // destination labels the provider-supported session type.
                SettingsRow {
                    id: activeSessionsBtn
                    label: typeof SessionService !== "undefined"
                        ? qsTr("Manage %1").arg(SessionService.sessionTypeLabel)
                        : qsTr("Manage Sessions")
                    description: qsTr("Review the active sessions for this account")
                    enabled: typeof SessionService !== "undefined"
                        && typeof AuthenticationService !== "undefined"
                        && AuthenticationService.authenticated
                    Accessible.role: Accessible.Button
                    onActivated: root.sessionsRequested()
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root._lastFocusedItem = this
                            flickable.ensureFocusVisible(this)
                        }
                    }
                    Keys.onUpPressed: function(event) {
                        logLevelCombo.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onDownPressed: function(event) {
                        signOutBtn.forceActiveFocus()
                        event.accepted = true
                    }

                    Text {
                        text: Icons.chevronRight
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.iconSizeLarge
                        color: activeSessionsBtn.highlighted ? Theme.textPrimary : Theme.textSecondary
                        Accessible.ignored: true
                    }
                }

                // Sign Out button

                ActionButton {
                    id: signOutBtn
                    variant: "destructive"
                    text: qsTr("Sign Out")
                    iconGlyph: Icons.logout
                    focusPolicy: Qt.StrongFocus
                    Layout.topMargin: Theme.spacingMedium
                    implicitHeight: Theme.buttonHeightLarge
                    onClicked: root.signOutRequested()
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root._lastFocusedItem = this
                            flickable.ensureFocusVisible(this)
                        }
                    }
                    Keys.onUpPressed: function(event) {
                        (activeSessionsBtn.enabled
                            ? activeSessionsBtn : logLevelCombo).forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onReturnPressed: root.signOutRequested()
                    Keys.onEnterPressed: root.signOutRequested()

                }
            }
        }
    }
}
