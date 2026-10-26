import QtQuick
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root
    signal requestReturnToRail()
    readonly property bool scrolled: flickable.scrolled
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
                    text: qsTr("Hero Banner")
                    Layout.fillWidth: true
                    Layout.topMargin: 0
                }

                SettingsToggleRow {
                    id: enabledRow; label: qsTr("Enable Hero Banner"); description: qsTr("Show featured media at the top of Home.")
                    checked: ConfigManager.heroBannerEnabled; ensureVisible: root.ensure
                    onToggled: function(value) { ConfigManager.heroBannerEnabled = value }
                    KeyNavigation.down: sourceCombo
                }
                SettingsRow {
                    label: qsTr("Hero source")
                    description: qsTr("Which items the hero banner features.")
                    dimmed: !sourceCombo.enabled
                    highlighted: sourceCombo.activeFocus || sourceCombo.popup.visible
                    focusTarget: sourceCombo
                    onActivated: if (sourceCombo.enabled) sourceCombo.popup.open()

                    SettingsComboBox {
                        id: sourceCombo
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

                SectionHeader {
                    level: "group"
                    text: qsTr("Layout")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                }

                SettingsRow {
                    label: qsTr("Logo position")
                    dimmed: !logoPlacementCombo.enabled
                    highlighted: logoPlacementCombo.activeFocus || logoPlacementCombo.popup.visible
                    focusTarget: logoPlacementCombo
                    onActivated: if (logoPlacementCombo.enabled) logoPlacementCombo.popup.open()

                    SettingsComboBox {
                        id: logoPlacementCombo
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
                }

                SettingsRow {
                    label: qsTr("Info position")
                    dimmed: !infoPlacementCombo.enabled
                    highlighted: infoPlacementCombo.activeFocus || infoPlacementCombo.popup.visible
                    focusTarget: infoPlacementCombo
                    onActivated: if (infoPlacementCombo.enabled) infoPlacementCombo.popup.open()

                    SettingsComboBox {
                        id: infoPlacementCombo
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
                }

                SectionHeader {
                    level: "group"
                    text: qsTr("Rotation")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
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
                SectionHeader {
                    level: "group"
                    text: qsTr("Content")
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
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
                    color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontPrimary
                    wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.topMargin: Theme.spacingSmall
                }
            }
        }
    }
}
