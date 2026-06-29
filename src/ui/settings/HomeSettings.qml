import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root
    signal requestReturnToRail()
    readonly property Item preferredEntryItem: enabledRow
    property Item _lastFocusedItem: null

    function enterFromRail() { (_lastFocusedItem || preferredEntryItem).forceActiveFocus() }
    function restoreFocus() { enterFromRail() }
    function ensure(item) { flickable.ensureFocusVisible(item); _lastFocusedItem = item }
    function sourceIndex() {
        var values = ["recentlyAdded", "continueWatching", "upNext", "library", "mixed"]
        return Math.max(0, values.indexOf(ConfigManager.heroBannerSource))
    }
    function setHidden(type, hidden) {
        var values = (ConfigManager.heroBannerHiddenItemTypes || []).slice()
        var index = values.indexOf(type)
        if (hidden && index < 0) values.push(type)
        if (!hidden && index >= 0) values.splice(index, 1)
        ConfigManager.heroBannerHiddenItemTypes = values
    }

    Keys.priority: Keys.AfterItem
    Keys.onLeftPressed: function(event) { requestReturnToRail(); event.accepted = true }
    Keys.onEscapePressed: function(event) { requestReturnToRail(); event.accepted = true }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.76)
        border.color: Theme.cardBorder
        border.width: Theme.borderWidth
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: 0
        Text { text: qsTr("Home"); font.pixelSize: Theme.fontSizeTitle; font.family: Theme.fontPrimary; font.bold: true; color: Theme.textPrimary; Layout.bottomMargin: Theme.spacingMedium }
        Flickable {
            id: flickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: contentColumn.implicitHeight + 2 * Theme.spacingSmall
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            function ensureFocusVisible(item) {
                var p = item.mapToItem(contentColumn, 0, 0)
                if (p.y < contentY + 40) contentY = Math.max(0, p.y - 40)
                else if (p.y + item.height > contentY + height - 40)
                    contentY = Math.min(Math.max(0, contentHeight - height), p.y + item.height - height + 40)
            }
            ColumnLayout {
                id: contentColumn
                x: Theme.spacingSmall
                y: Theme.spacingSmall
                width: flickable.width - 2 * Theme.spacingSmall
                spacing: Theme.spacingLarge

                SettingsToggleRow {
                    id: enabledRow; label: qsTr("Enable Hero Banner"); description: qsTr("Show featured media at the top of Home.")
                    checked: ConfigManager.heroBannerEnabled; ensureVisible: root.ensure
                    onToggled: function(value) { ConfigManager.heroBannerEnabled = value }
                    KeyNavigation.down: sourceCombo
                }
                SettingsComboBox {
                    id: sourceCombo
                    Layout.preferredWidth: Math.round(300 * Theme.layoutScale)
                    model: [qsTr("Recently Added"), qsTr("Continue Watching"), qsTr("Up Next"), qsTr("Library"), qsTr("Mixed")]

                    property bool _syncingFromConfig: false

                    function sourceValues() {
                        return ["recentlyAdded", "continueWatching", "upNext", "library", "mixed"]
                    }
                    function applySourceAt(index) {
                        if (_syncingFromConfig || index < 0)
                            return
                        var values = sourceValues()
                        if (index >= values.length)
                            return
                        ConfigManager.heroBannerSource = values[index]
                    }
                    function syncFromConfig() {
                        _syncingFromConfig = true
                        currentIndex = root.sourceIndex()
                        _syncingFromConfig = false
                    }

                    Component.onCompleted: syncFromConfig()

                    Connections {
                        target: ConfigManager
                        function onHeroBannerSourceChanged() { sourceCombo.syncFromConfig() }
                    }

                    onSelectionAccepted: function(index) { applySourceAt(index) }
                    onActivated: function(index) { applySourceAt(index) }
                    onActiveFocusChanged: if (activeFocus) root.ensure(this)
                    KeyNavigation.up: enabledRow; KeyNavigation.down: maxItemsRow
                }
                SettingsSpinBoxRow {
                    id: maxItemsRow; label: qsTr("Maximum hero items"); from: 1; to: 25; value: ConfigManager.heroBannerMaxItems
                    ensureVisible: root.ensure; onSpinBoxValueChanged: ConfigManager.heroBannerMaxItems = newValue
                    KeyNavigation.up: sourceCombo; KeyNavigation.down: episodeSynopsisRow
                }
                SettingsToggleRow {
                    id: episodeSynopsisRow
                    label: qsTr("Use episode synopses")
                    description: qsTr("Show episode descriptions in the hero banner when available; otherwise use the series description.")
                    checked: ConfigManager.heroBannerEpisodeSynopsisEnabled
                    ensureVisible: root.ensure
                    onToggled: ConfigManager.heroBannerEpisodeSynopsisEnabled = value
                    KeyNavigation.up: maxItemsRow
                    KeyNavigation.down: logoPlacementCombo
                }

                Text {
                    text: qsTr("Layout")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    font.bold: true
                    color: Theme.textPrimary
                    Layout.topMargin: Theme.spacingSmall
                }

                Text {
                    text: qsTr("Logo position")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                }

                SettingsComboBox {
                    id: logoPlacementCombo
                    Layout.preferredWidth: Math.round(300 * Theme.layoutScale)
                    model: [
                        qsTr("Bottom left"),
                        qsTr("Bottom right"),
                        qsTr("Bottom center"),
                        qsTr("Top left"),
                        qsTr("Top right"),
                        qsTr("Top center"),
                        qsTr("Center"),
                        qsTr("Center (large)")
                    ]

                    property bool _syncingFromConfig: false
                    function placementValues() {
                        return ["bottomLeft", "bottomRight", "bottomCenter", "topLeft", "topRight", "topCenter", "center", "centerLarge"]
                    }
                    function placementIndex() {
                        return Math.max(0, placementValues().indexOf(ConfigManager.heroBannerLogoPlacement))
                    }
                    function applyPlacementAt(index) {
                        if (_syncingFromConfig || index < 0) return
                        var values = placementValues()
                        if (index >= values.length) return
                        ConfigManager.heroBannerLogoPlacement = values[index]
                    }
                    function syncFromConfig() {
                        _syncingFromConfig = true
                        currentIndex = placementIndex()
                        _syncingFromConfig = false
                    }

                    Component.onCompleted: syncFromConfig()
                    Connections {
                        target: ConfigManager
                        function onHeroBannerLogoPlacementChanged() { logoPlacementCombo.syncFromConfig() }
                    }
                    onSelectionAccepted: function(index) { applyPlacementAt(index) }
                    onActivated: function(index) { applyPlacementAt(index) }
                    onActiveFocusChanged: if (activeFocus) root.ensure(this)
                    KeyNavigation.up: episodeSynopsisRow
                    KeyNavigation.down: infoPlacementCombo
                }

                Text {
                    text: qsTr("Info position")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                }

                SettingsComboBox {
                    id: infoPlacementCombo
                    Layout.preferredWidth: Math.round(300 * Theme.layoutScale)
                    model: [
                        qsTr("Bottom left"),
                        qsTr("Bottom right"),
                        qsTr("Bottom center"),
                        qsTr("Top left"),
                        qsTr("Top right"),
                        qsTr("Top center"),
                        qsTr("Center")
                    ]

                    property bool _syncingFromConfig: false
                    function placementValues() {
                        return ["bottomLeft", "bottomRight", "bottomCenter", "topLeft", "topRight", "topCenter", "center"]
                    }
                    function placementIndex() {
                        return Math.max(0, placementValues().indexOf(ConfigManager.heroBannerInfoPlacement))
                    }
                    function applyPlacementAt(index) {
                        if (_syncingFromConfig || index < 0) return
                        var values = placementValues()
                        if (index >= values.length) return
                        ConfigManager.heroBannerInfoPlacement = values[index]
                    }
                    function syncFromConfig() {
                        _syncingFromConfig = true
                        currentIndex = placementIndex()
                        _syncingFromConfig = false
                    }

                    Component.onCompleted: syncFromConfig()
                    Connections {
                        target: ConfigManager
                        function onHeroBannerInfoPlacementChanged() { infoPlacementCombo.syncFromConfig() }
                    }
                    onSelectionAccepted: function(index) { applyPlacementAt(index) }
                    onActivated: function(index) { applyPlacementAt(index) }
                    onActiveFocusChanged: if (activeFocus) root.ensure(this)
                    KeyNavigation.up: logoPlacementCombo
                    KeyNavigation.down: autoCycleRow
                }

                SettingsToggleRow {
                    id: autoCycleRow; label: qsTr("Auto-cycle"); checked: ConfigManager.heroBannerAutoCycleEnabled; ensureVisible: root.ensure
                    onToggled: ConfigManager.heroBannerAutoCycleEnabled = value
                    KeyNavigation.up: infoPlacementCombo; KeyNavigation.down: intervalRow
                }
                SettingsSliderRow {
                    id: intervalRow; label: qsTr("Auto-cycle interval"); from: 3; to: 120; stepSize: 1
                    value: Math.round(ConfigManager.heroBannerAutoCycleInterval / 1000); unit: "s"; ensureVisible: root.ensure
                    onSliderValueChanged: ConfigManager.heroBannerAutoCycleInterval = Math.round(newValue * 1000)
                    KeyNavigation.up: autoCycleRow; KeyNavigation.down: backdropRow
                }
                SettingsToggleRow {
                    id: backdropRow; label: qsTr("Sync page backdrop"); checked: ConfigManager.heroBannerBackdropSyncEnabled; ensureVisible: root.ensure
                    onToggled: ConfigManager.heroBannerBackdropSyncEnabled = value
                    KeyNavigation.up: intervalRow; KeyNavigation.down: hideMoviesRow
                }
                SettingsToggleRow {
                    id: hideMoviesRow; label: qsTr("Hide movies"); checked: (ConfigManager.heroBannerHiddenItemTypes || []).indexOf("Movie") >= 0; ensureVisible: root.ensure
                    onToggled: root.setHidden("Movie", value)
                    KeyNavigation.up: backdropRow; KeyNavigation.down: hideSeriesRow
                }
                SettingsToggleRow {
                    id: hideSeriesRow; label: qsTr("Hide series"); checked: (ConfigManager.heroBannerHiddenItemTypes || []).indexOf("Series") >= 0; ensureVisible: root.ensure
                    onToggled: root.setHidden("Series", value)
                    KeyNavigation.up: hideMoviesRow; KeyNavigation.down: unwatchedRow
                }
                SettingsToggleRow {
                    id: unwatchedRow; label: qsTr("Library: unwatched only"); checked: ConfigManager.heroBannerLibraryUnwatchedOnly; ensureVisible: root.ensure
                    onToggled: ConfigManager.heroBannerLibraryUnwatchedOnly = value
                    KeyNavigation.up: hideSeriesRow
                }
                Text {
                    text: qsTr("Library source uses all visible libraries unless specific library IDs are configured.")
                    color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }
        }
    }
}
