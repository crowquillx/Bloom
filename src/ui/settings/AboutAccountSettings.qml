import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()
    signal signOutRequested()

    readonly property Item preferredEntryItem: autoUpdateCheckToggle
    property Item _lastFocusedItem: null

    property string appVersion: ""
    property string appBuildChannel: ""
    property string appBuildId: ""
    property string qtVersion: ""

    function enterFromRail() {
        var target = _lastFocusedItem || preferredEntryItem
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

    Keys.priority: Keys.AfterItem
    Keys.onLeftPressed: requestReturnToRail()
    Keys.onEscapePressed: requestReturnToRail()

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
            text: qsTr("About & Account")
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
                spacing: Theme.spacingMedium

                // ── Group 1: About Info ──

                SettingsInfoRow {
                    label: qsTr("Version")
                    value: root.appVersion
                }

                SettingsInfoRow {
                    label: qsTr("Qt Version")
                    value: root.qtVersion
                }

                Text {
                    text: qsTr("Bloom is a modern Jellyfin client designed for the 10-foot TV experience.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                SettingsGroupDivider { Layout.fillWidth: true }

                // ── Group 2: Updates ──

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
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Math.round(4 * Theme.layoutScale)

                    Text {
                        text: qsTr("Update Channel")
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }

                    Text {
                        text: qsTr("Choose which release channel to follow for updates.")
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    ComboBox {
                        id: updateChannelCombo
                        focusPolicy: Qt.StrongFocus
                        model: ["Stable", "Development"]
                        Layout.preferredWidth: Math.round(200 * Theme.layoutScale)

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
                            ConfigManager.updateChannel = channel
                            UpdateService.setChannel(channel)
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
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                checkUpdatesBtn.forceActiveFocus()
                            }
                        }
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()

                        background: Rectangle {
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: Theme.inputBackground
                            border.color: updateChannelCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: updateChannelCombo.activeFocus ? 2 : 1
                        }

                        contentItem: Text {
                            text: updateChannelCombo.displayText
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                        }

                        delegate: ItemDelegate {
                            width: updateChannelCombo.width
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
                            highlighted: ListView.isCurrentItem || updateChannelCombo.highlightedIndex === index
                        }

                        popup: Popup {
                            y: updateChannelCombo.height + 5
                            width: updateChannelCombo.width
                            implicitHeight: contentItem.implicitHeight
                            padding: 1

                            onOpened: {
                                channelPopupList.currentIndex = updateChannelCombo.highlightedIndex >= 0 ? updateChannelCombo.highlightedIndex : updateChannelCombo.currentIndex
                                channelPopupList.forceActiveFocus()
                            }
                            onClosed: updateChannelCombo.forceActiveFocus()

                            contentItem: ListView {
                                id: channelPopupList
                                clip: true
                                implicitHeight: contentHeight
                                model: updateChannelCombo.popup.visible ? updateChannelCombo.delegateModel : null
                                currentIndex: updateChannelCombo.highlightedIndex >= 0 ? updateChannelCombo.highlightedIndex : updateChannelCombo.currentIndex
                                ScrollIndicator.vertical: ScrollIndicator { }
                                Keys.onReturnPressed: { updateChannelCombo.currentIndex = currentIndex; updateChannelCombo.popup.close() }
                                Keys.onEnterPressed: { updateChannelCombo.currentIndex = currentIndex; updateChannelCombo.popup.close() }
                                Keys.onEscapePressed: updateChannelCombo.popup.close()
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

                // Button Row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    Button {
                        id: checkUpdatesBtn
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
                        KeyNavigation.up: updateChannelCombo
                        KeyNavigation.down: openDownloadPageBtn.visible ? openDownloadPageBtn : serverInfoRow
                        contentItem: Text { text: checkUpdatesBtn.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: checkUpdatesBtn.enabled ? Theme.textPrimary : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { implicitHeight: Theme.buttonHeightSmall; radius: Theme.radiusSmall; color: checkUpdatesBtn.activeFocus || checkUpdatesBtn.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground; border.color: checkUpdatesBtn.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder; border.width: checkUpdatesBtn.activeFocus ? 2 : Theme.buttonBorderWidth; opacity: checkUpdatesBtn.enabled ? 1.0 : 0.5 }
                    }

                    Button {
                        id: downloadInstallBtn
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
                        contentItem: Text { text: downloadInstallBtn.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: downloadInstallBtn.enabled ? Theme.textPrimary : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { implicitHeight: Theme.buttonHeightSmall; radius: Theme.radiusSmall; color: downloadInstallBtn.activeFocus || downloadInstallBtn.hovered ? Qt.rgba(0.4, 0.6, 1, 0.2) : "transparent"; border.color: downloadInstallBtn.activeFocus ? Theme.focusBorder : Theme.accentPrimary; border.width: downloadInstallBtn.activeFocus ? 2 : 1; opacity: downloadInstallBtn.enabled ? 1.0 : 0.5 }
                    }

                    Button {
                        id: openInstallerBtn
                        focusPolicy: Qt.StrongFocus
                        visible: UpdateService.updateAvailable && !UpdateService.applySupported && UpdateService.installerUrl.length > 0
                        text: qsTr("Open Installer")
                        onClicked: UpdateService.openInstallerAsset()
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }
                        contentItem: Text { text: openInstallerBtn.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: openInstallerBtn.enabled ? Theme.textPrimary : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { implicitHeight: Theme.buttonHeightSmall; radius: Theme.radiusSmall; color: openInstallerBtn.activeFocus || openInstallerBtn.hovered ? Qt.rgba(0.4, 0.6, 1, 0.2) : "transparent"; border.color: openInstallerBtn.activeFocus ? Theme.focusBorder : Theme.accentPrimary; border.width: openInstallerBtn.activeFocus ? 2 : 1; opacity: openInstallerBtn.enabled ? 1.0 : 0.5 }
                    }

                    Button {
                        id: openPortableBtn
                        focusPolicy: Qt.StrongFocus
                        visible: UpdateService.updateAvailable && !UpdateService.applySupported && UpdateService.portableUrl.length > 0
                        text: qsTr("Open Portable ZIP")
                        onClicked: UpdateService.openPortableAsset()
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }
                        contentItem: Text { text: openPortableBtn.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: openPortableBtn.enabled ? Theme.textPrimary : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { implicitHeight: Theme.buttonHeightSmall; radius: Theme.radiusSmall; color: openPortableBtn.activeFocus || openPortableBtn.hovered ? Qt.rgba(0.4, 0.6, 1, 0.2) : "transparent"; border.color: openPortableBtn.activeFocus ? Theme.focusBorder : Theme.accentPrimary; border.width: openPortableBtn.activeFocus ? 2 : 1; opacity: openPortableBtn.enabled ? 1.0 : 0.5 }
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
                    radius: Theme.radiusSmall
                    color: Theme.inputBackground
                    border.color: Theme.cardBorder
                    border.width: 1

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
                Button {
                    id: openDownloadPageBtn
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
                    KeyNavigation.up: checkUpdatesBtn
                    KeyNavigation.down: serverInfoRow
                    contentItem: Text { text: openDownloadPageBtn.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: openDownloadPageBtn.enabled ? Theme.textPrimary : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { implicitHeight: Theme.buttonHeightSmall; radius: Theme.radiusSmall; color: openDownloadPageBtn.activeFocus || openDownloadPageBtn.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground; border.color: openDownloadPageBtn.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder; border.width: openDownloadPageBtn.activeFocus ? 2 : Theme.buttonBorderWidth; opacity: openDownloadPageBtn.enabled ? 1.0 : 0.5 }
                }

                SettingsGroupDivider { Layout.fillWidth: true }

                // ── Group 3: Account ──

                SettingsInfoRow {
                    id: serverInfoRow
                    label: qsTr("Server")
                    value: ConfigManager.serverUrl || qsTr("Not connected")
                }

                SettingsInfoRow {
                    label: qsTr("User")
                    value: ConfigManager.username || qsTr("Unknown")
                }

                // Sign Out button
                Button {
                    id: signOutBtn
                    focusPolicy: Qt.StrongFocus
                    Layout.topMargin: Theme.spacingMedium
                    onClicked: root.signOutRequested()
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root._lastFocusedItem = this
                            flickable.ensureFocusVisible(this)
                        }
                    }
                    KeyNavigation.up: openDownloadPageBtn.visible ? openDownloadPageBtn : checkUpdatesBtn
                    Keys.onReturnPressed: root.signOutRequested()
                    Keys.onEnterPressed: root.signOutRequested()

                    contentItem: RowLayout {
                        spacing: Theme.spacingSmall

                        Text {
                            text: "\uf2f5"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.fontSizeBody
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            text: qsTr("Sign Out")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: signOutBtn.activeFocus || signOutBtn.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground
                        border.color: signOutBtn.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
                        border.width: signOutBtn.activeFocus ? 2 : Theme.buttonBorderWidth
                        opacity: signOutBtn.enabled ? 1.0 : 0.5
                    }
                }
            }
        }
    }
}
