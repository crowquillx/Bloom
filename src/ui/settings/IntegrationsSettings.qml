import QtQuick
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()
    readonly property bool scrolled: flickable.scrolled

    readonly property Item preferredEntryItem: mdbListApiKeyInput
    property Item _lastFocusedItem: null

    function enterFromRail() {
        var target = (_lastFocusedItem && _lastFocusedItem.visible) ? _lastFocusedItem : preferredEntryItem
        if (target) target.forceActiveFocus()
    }

    function restoreFocus() {
        enterFromRail()
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

                // --- MDBList Section ---
                SectionHeader {
                    level: "group"
                    text: qsTr("MDBList")
                    Layout.fillWidth: true
                    Layout.topMargin: 0
                }

                Text {
                    text: qsTr("Connect your MDBList account to display enhanced ratings from IMDb, Metacritic, and other sources alongside content from your connected server.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.spacingSmall
                }

                SettingsTextInputRow {
                    id: mdbListApiKeyInput
                    label: qsTr("MDBList API Key")
                    placeholderText: qsTr("API Key")
                    echoMode: TextInput.Password
                    text: ConfigManager.mdbListApiKey
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    keyUpTarget: null
                    keyDownTarget: seerrUrlInput.input
                    onEditingFinished: ConfigManager.mdbListApiKey = text
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                }

                // --- Jellyseerr Section ---
                SectionHeader {
                    level: "group"
                    text: qsTr("Jellyseerr")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                Text {
                    text: qsTr("Connect to your Jellyseerr instance to search for new content and submit requests directly from Bloom.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.spacingSmall
                }

                SettingsTextInputRow {
                    id: seerrUrlInput
                    label: qsTr("Seerr URL")
                    placeholderText: "http://localhost:5055"
                    text: ConfigManager.seerrBaseUrl
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    keyUpTarget: mdbListApiKeyInput.input
                    keyDownTarget: seerrApiKeyInput.input
                    onEditingFinished: ConfigManager.seerrBaseUrl = text
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                }

                SettingsTextInputRow {
                    id: seerrApiKeyInput
                    label: qsTr("Seerr API Key")
                    placeholderText: qsTr("API Key")
                    echoMode: TextInput.Password
                    text: ConfigManager.seerrApiKey
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    keyUpTarget: seerrUrlInput.input
                    keyDownTarget: externalSegmentsToggle
                    onEditingFinished: ConfigManager.seerrApiKey = text
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                }

                // --- Media Segments Section ---
                SectionHeader {
                    level: "group"
                    text: qsTr("Media Segments")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                Text {
                    text: qsTr("Use external segment providers to fill missing intro, recap, and credits markers when your server does not provide them.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.spacingSmall
                }

                SettingsToggleRow {
                    id: externalSegmentsToggle
                    label: qsTr("Use External Segment Providers")
                    description: qsTr("Server-provided segments are always preferred; external providers only fill missing segment types.")
                    checked: ConfigManager.externalSegmentProvidersEnabled
                    ensureVisible: function(item) { flickable.ensureFocusVisible(item) }
                    KeyNavigation.up: seerrApiKeyInput.input
                    KeyNavigation.down: null
                    onToggled: function(value) { ConfigManager.externalSegmentProvidersEnabled = value }
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                }
            }
        }
    }
}
