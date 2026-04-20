import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    signal requestReturnToRail()

    readonly property Item preferredEntryItem: mdbListApiKeyInput
    property Item _lastFocusedItem: null

    function enterFromRail() {
        var target = _lastFocusedItem || preferredEntryItem
        if (target) target.forceActiveFocus()
    }

    function restoreFocus() {
        enterFromRail()
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
            text: qsTr("Integrations")
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
                if (itemY < viewTop + padding) {
                    contentY = Math.max(0, itemY - padding)
                } else if (itemY + itemHeight > viewBottom - padding) {
                    contentY = Math.min(contentHeight - height, itemY + itemHeight - height + padding)
                }
            }

            ColumnLayout {
                id: contentColumn
                x: Theme.spacingSmall
                y: Theme.spacingSmall
                width: flickable.width - 2 * Theme.spacingSmall
                spacing: Theme.spacingMedium

                // --- MDBList Section ---
                Text {
                    text: qsTr("MDBList")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                }

                Text {
                    text: qsTr("Connect your MDBList account to display enhanced ratings from IMDb, Metacritic, and other sources alongside Jellyfin content.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
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

                SettingsGroupDivider { Layout.fillWidth: true }

                // --- Jellyseerr Section ---
                Text {
                    text: qsTr("Jellyseerr")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                }

                Text {
                    text: qsTr("Connect to your Jellyseerr instance to search for new content and submit requests directly from Bloom.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
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
                    keyDownTarget: null
                    onEditingFinished: ConfigManager.seerrApiKey = text
                    onActiveFocusChanged: { if (activeFocus) root._lastFocusedItem = this }
                }
            }
        }
    }
}
