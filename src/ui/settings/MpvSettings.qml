import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    property var profileNames: []
    property Item _lastFocusedItem: null
    readonly property Item preferredEntryItem: defaultProfileCombo

    signal requestReturnToRail()
    readonly property bool scrolled: flickable.scrolled
    signal openNewProfileDialog(Item returnFocusTarget)
    signal openImportConfigDialog(Item returnFocusTarget)
    signal openDeleteProfileDialog(string profileName, Item returnFocusTarget)
    signal openRenameProfileDialog(string profileName, Item returnFocusTarget)

    function enterFromRail() {
        var target = (_lastFocusedItem && _lastFocusedItem.visible) ? _lastFocusedItem : defaultProfileCombo
        if (target) target.forceActiveFocus()
    }

    function restoreFocus() {
        enterFromRail()
    }

    function selectProfile(name) {
        Qt.callLater(function() {
            var idx = editProfileCombo.model ? editProfileCombo.model.indexOf(name) : -1
            if (idx >= 0) {
                profileEditorToggle.expanded = true
                editProfileCombo.currentIndex = idx
            }
        })
    }

    function isBuiltInProfileName(name) {
        return name === "Low Quality"
            || name === "Medium Quality"
            || name === "High Quality"
            || name === "ArtCNN"
            || name === "ArtCNN-Deband"
            || name === "nnedi3"
            || name === "nnedi3-deband"
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
            contentHeight: outerColumn.implicitHeight + Theme.pageMarginBottom

            ColumnLayout {
                id: outerColumn
                x: Theme.spacingMedium
                y: Theme.spacingSmall
                width: flickable.width - 2 * Theme.spacingMedium
                spacing: Theme.spacingSmall

                SectionHeader {
                    level: "group"
                    text: qsTr("Profiles")
                    Layout.fillWidth: true
                }

                SettingsRow {
                    label: qsTr("Default Profile")
                    description: qsTr("Profile used when no library or series override is set")
                    dimmed: !defaultProfileCombo.enabled
                    highlighted: defaultProfileCombo.activeFocus || defaultProfileCombo.popup.visible
                    focusTarget: defaultProfileCombo
                    onActivated: if (defaultProfileCombo.enabled) defaultProfileCombo.popup.open()

                    SettingsComboBox {
                        id: defaultProfileCombo
                        model: root.profileNames
                        focusPolicy: Qt.StrongFocus

                        property bool initialized: false
                        property bool updatingSelection: false

                        function refreshSelection() {
                            var options = model || []
                            if (!options || options.length === 0) {
                                updatingSelection = true
                                currentIndex = -1
                                updatingSelection = false
                                return
                            }
                            updatingSelection = true
                            var idx = options.indexOf(ConfigManager.defaultProfileName)
                            if (idx < 0) idx = options.indexOf("Medium Quality")
                            currentIndex = idx >= 0 ? idx : 0
                            updatingSelection = false
                        }

                        function applySelection(index) {
                            var options = model || []
                            if (index < 0 || index >= options.length) return
                            var selectedName = options[index]
                            if (selectedName && selectedName !== ConfigManager.defaultProfileName) {
                                ConfigManager.defaultProfileName = selectedName
                            }
                        }

                        Component.onCompleted: {
                            refreshSelection()
                            initialized = true
                        }

                        onModelChanged: {
                            if (!initialized) return
                            refreshSelection()
                        }

                        onActivated: function(index) {
                            applySelection(index)
                        }
                        onSelectionAccepted: function(index) {
                            applySelection(index)
                        }

                        Connections {
                            target: ConfigManager
                            function onDefaultProfileNameChanged() {
                                defaultProfileCombo.refreshSelection()
                            }
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        // Top of the page: Up goes to the navigation bar.
                        Keys.onUpPressed: function(event) { event.accepted = false }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                profileEditorToggle.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()
                    }
                }

            // ========================================
            // 2. Profile Editor Toggle
            // ========================================
            SettingsRow {
                id: profileEditorToggle
                property bool expanded: false
                label: qsTr("Edit Profiles")
                description: qsTr("Create, rename, import and tune mpv profiles")
                Accessible.role: Accessible.Button
                Accessible.name: (expanded ? qsTr("Collapse") : qsTr("Expand")) + " " + label

                onActiveFocusChanged: {
                    if (activeFocus) {
                        root._lastFocusedItem = this
                        flickable.ensureFocusVisible(this)
                    }
                }

                KeyNavigation.up: defaultProfileCombo
                KeyNavigation.down: profileEditorToggle.expanded ? editProfileCombo : libraryProfilesToggle

                onActivated: expanded = !expanded

                Text {
                    text: profileEditorToggle.expanded ? Icons.expandLess : Icons.expandMore
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.iconSizeLarge
                    color: profileEditorToggle.highlighted ? Theme.textPrimary : Theme.textSecondary
                    Accessible.ignored: true
                }
            }

            // ========================================
            // 3. Profile Editor Content
            // ========================================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                visible: profileEditorToggle.expanded

                // Select Profile to Edit
                SettingsRow {
                    label: qsTr("Select Profile to Edit")
                    dimmed: !editProfileCombo.enabled
                    highlighted: editProfileCombo.activeFocus || editProfileCombo.popup.visible
                    focusTarget: editProfileCombo
                    onActivated: if (editProfileCombo.enabled) editProfileCombo.popup.open()

                    SettingsComboBox {
                        id: editProfileCombo
                        model: root.profileNames
                        focusPolicy: Qt.StrongFocus

                        property bool initialized: false
                        property bool updatingSelection: false

                        function refreshSelection() {
                            if (!model || model.length === 0) {
                                currentIndex = -1
                                return
                            }
                            updatingSelection = true
                            if (currentIndex < 0 || currentIndex >= model.length) {
                                currentIndex = 0
                            }
                            updatingSelection = false
                        }

                        Component.onCompleted: {
                            refreshSelection()
                            initialized = true
                        }

                        onModelChanged: {
                            if (!initialized) return
                            refreshSelection()
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        Keys.onUpPressed: function(event) {
                            if (!popup.visible) {
                                profileEditorToggle.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                profileEditor.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onRightPressed: function(event) {
                            if (!popup.visible) {
                                newProfileBtn.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onReturnPressed: function(event) {
                            if (!popup.visible) {
                                popup.open()
                                event.accepted = true
                            }
                        }
                        Keys.onEnterPressed: function(event) {
                            if (!popup.visible) {
                                popup.open()
                                event.accepted = true
                            }
                        }
                    }
                }

                // Button Row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingXSmall
                    spacing: Theme.controlSpacing

                    ActionButton {
                        id: newProfileBtn
                        variant: "secondary"
                        text: qsTr("+ New Profile")
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Create New Profile")
                        focusPolicy: Qt.StrongFocus

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        Keys.onUpPressed: profileEditorToggle.forceActiveFocus()
                        Keys.onDownPressed: profileEditor.forceActiveFocus()
                        Keys.onLeftPressed: editProfileCombo.forceActiveFocus()
                        Keys.onRightPressed: duplicateProfileBtn.forceActiveFocus()

                        onClicked: root.openNewProfileDialog(newProfileBtn)
                        Keys.onReturnPressed: root.openNewProfileDialog(newProfileBtn)
                        Keys.onEnterPressed: root.openNewProfileDialog(newProfileBtn)
                    }

                    ActionButton {
                        id: duplicateProfileBtn
                        variant: "secondary"
                        text: qsTr("Duplicate")
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Duplicate selected profile")
                        focusPolicy: Qt.StrongFocus
                        enabled: editProfileCombo.currentText !== ""

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        Keys.onUpPressed: profileEditorToggle.forceActiveFocus()
                        Keys.onDownPressed: profileEditor.forceActiveFocus()
                        Keys.onLeftPressed: newProfileBtn.forceActiveFocus()
                        Keys.onRightPressed: {
                            if (renameProfileBtn.enabled) {
                                renameProfileBtn.forceActiveFocus()
                                return
                            }
                            if (importConfigBtn.enabled) {
                                importConfigBtn.forceActiveFocus()
                            }
                        }

                        function duplicateSelectedProfile() {
                            var sourceName = editProfileCombo.currentText
                            if (!sourceName || sourceName === "")
                                return

                            var sourceProfile = ConfigManager.getMpvProfile(sourceName)
                            if (!sourceProfile)
                                return

                            var names = ConfigManager.mpvProfileNames || []
                            var baseName = sourceName + " Copy"
                            var candidate = baseName
                            var suffix = 2
                            while (names.indexOf(candidate) >= 0) {
                                candidate = baseName + " " + suffix
                                suffix++
                            }

                            ConfigManager.setMpvProfile(candidate, sourceProfile)

                            Qt.callLater(function() {
                                var idx = editProfileCombo.model ? editProfileCombo.model.indexOf(candidate) : -1
                                if (idx >= 0) {
                                    editProfileCombo.currentIndex = idx
                                }
                            })
                        }

                        onClicked: duplicateSelectedProfile()
                        Keys.onReturnPressed: duplicateSelectedProfile()
                        Keys.onEnterPressed: duplicateSelectedProfile()
                    }

                    ActionButton {
                        id: renameProfileBtn
                        variant: "secondary"
                        text: qsTr("Rename")
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Rename selected profile")
                        focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                        enabled: {
                            var name = editProfileCombo.currentText || ""
                            return name !== "" && !root.isBuiltInProfileName(name)
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        Keys.onUpPressed: profileEditorToggle.forceActiveFocus()
                        Keys.onDownPressed: profileEditor.forceActiveFocus()
                        Keys.onLeftPressed: duplicateProfileBtn.forceActiveFocus()
                        Keys.onRightPressed: importConfigBtn.forceActiveFocus()

                        onEnabledChanged: {
                            if (!enabled && activeFocus) {
                                duplicateProfileBtn.forceActiveFocus()
                            }
                        }

                        function requestRenameSelectedProfile() {
                            if (!enabled)
                                return
                            root.openRenameProfileDialog(editProfileCombo.currentText, renameProfileBtn)
                        }

                        onClicked: requestRenameSelectedProfile()
                        Keys.onReturnPressed: requestRenameSelectedProfile()
                        Keys.onEnterPressed: requestRenameSelectedProfile()
                    }

                    ActionButton {
                        id: importConfigBtn
                        variant: "secondary"
                        text: qsTr("Import Config")
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Import mpv.conf as profile")
                        focusPolicy: Qt.StrongFocus

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        Keys.onUpPressed: profileEditorToggle.forceActiveFocus()
                        Keys.onDownPressed: profileEditor.forceActiveFocus()
                        Keys.onLeftPressed: {
                            if (renameProfileBtn.enabled) {
                                renameProfileBtn.forceActiveFocus()
                            } else {
                                duplicateProfileBtn.forceActiveFocus()
                            }
                        }
                        Keys.onRightPressed: {
                            if (deleteProfileBtn.enabled) {
                                deleteProfileBtn.forceActiveFocus()
                            }
                        }

                        onClicked: root.openImportConfigDialog(importConfigBtn)
                        Keys.onReturnPressed: root.openImportConfigDialog(importConfigBtn)
                        Keys.onEnterPressed: root.openImportConfigDialog(importConfigBtn)
                    }

                    ActionButton {
                        id: deleteProfileBtn
                        variant: "destructive"
                        text: qsTr("Delete")
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Delete selected profile")
                        focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                        enabled: {
                            var name = editProfileCombo.currentText || ""
                            return name !== "" && !root.isBuiltInProfileName(name)
                        }

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root._lastFocusedItem = this
                                flickable.ensureFocusVisible(this)
                            }
                        }

                        Keys.onUpPressed: profileEditorToggle.forceActiveFocus()
                        Keys.onDownPressed: profileEditor.forceActiveFocus()
                        Keys.onLeftPressed: {
                            if (renameProfileBtn.enabled) {
                                importConfigBtn.forceActiveFocus()
                            } else {
                                duplicateProfileBtn.forceActiveFocus()
                            }
                        }

                        onEnabledChanged: {
                            if (!enabled && activeFocus) {
                                duplicateProfileBtn.forceActiveFocus()
                            }
                        }

                        function requestDeleteSelectedProfile() {
                            if (!enabled)
                                return
                            root.openDeleteProfileDialog(editProfileCombo.currentText, deleteProfileBtn)
                        }

                        onClicked: requestDeleteSelectedProfile()
                        Keys.onReturnPressed: requestDeleteSelectedProfile()
                        Keys.onEnterPressed: requestDeleteSelectedProfile()
                    }
                }

                // Profile Editor
                MpvProfileEditor {
                    id: profileEditor
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingLarge
                    profileName: editProfileCombo.currentText || ""
                    parentFlickable: flickable
                    onDeleteRequested: root.openDeleteProfileDialog(profileName, profileEditor)
                    onNavigateOut: function(direction) {
                        if (direction === "up") newProfileBtn.forceActiveFocus()
                        else if (direction === "down") libraryProfilesToggle.forceActiveFocus()
                    }
                }
            }

            // ========================================
            // 4. Library Profiles Toggle
            // ========================================
            SettingsRow {
                id: libraryProfilesToggle
                property bool expanded: false
                label: qsTr("Library Profiles")
                description: qsTr("Use a different profile for each library")
                Accessible.role: Accessible.Button
                Accessible.name: (expanded ? qsTr("Collapse") : qsTr("Expand")) + " " + label

                onActiveFocusChanged: {
                    if (activeFocus) {
                        root._lastFocusedItem = this
                        flickable.ensureFocusVisible(this)
                    }
                }

                Keys.onUpPressed: function(event) {
                    if (profileEditorToggle.expanded && profileEditor) {
                        profileEditor.focusBottomAnchor()
                    } else {
                        profileEditorToggle.forceActiveFocus()
                    }
                    event.accepted = true
                }

                Keys.onDownPressed: function(event) {
                    if (libraryProfilesToggle.expanded && libraryProfilesRepeater.count > 0) {
                        libraryProfilesRepeater.itemAt(0).profileCombo.forceActiveFocus()
                    }
                    event.accepted = true
                }

                onActivated: expanded = !expanded

                Text {
                    text: libraryProfilesToggle.expanded ? Icons.expandLess : Icons.expandMore
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.iconSizeLarge
                    color: libraryProfilesToggle.highlighted ? Theme.textPrimary : Theme.textSecondary
                    Accessible.ignored: true
                }
            }

            // ========================================
            // 5. Library Profiles Content
            // ========================================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                visible: libraryProfilesToggle.expanded

                Text {
                    text: qsTr("Assign a profile to each library. Leave as 'Use Default' to use the default profile.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Repeater {
                    id: libraryProfilesRepeater
                    model: LibraryViewModel.views

                    delegate: SettingsRow {
                        id: libraryDelegate
                        required property var modelData
                        required property int index

                        property string name: modelData.name || ""
                        property string itemId: modelData.itemId || ""
                        property alias profileCombo: libraryProfileCombo
                        property var profileOptions: {
                            var names = root.profileNames || []
                            return [qsTr("Use Default")].concat(names)
                        }

                        label: libraryDelegate.name
                        highlighted: libraryProfileCombo.activeFocus || libraryProfileCombo.popup.visible
                        focusTarget: libraryProfileCombo
                        onActivated: libraryProfileCombo.popup.open()

                        SettingsComboBox {
                            id: libraryProfileCombo
                            model: libraryDelegate.profileOptions
                            focusPolicy: Qt.StrongFocus

                            property bool initialized: false
                            property bool updatingSelection: false

                            function refreshSelection() {
                                if (!libraryDelegate.profileOptions || libraryDelegate.profileOptions.length === 0) {
                                    updatingSelection = true
                                    currentIndex = 0
                                    updatingSelection = false
                                    return
                                }
                                var assigned = ConfigManager.getLibraryProfile(libraryDelegate.itemId)
                                var options = libraryDelegate.profileOptions || []
                                updatingSelection = true
                                if (assigned === "") {
                                    currentIndex = 0
                                } else {
                                    var idx = options.indexOf(assigned)
                                    currentIndex = idx >= 0 ? idx : 0
                                }
                                updatingSelection = false
                            }

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root._lastFocusedItem = this
                                    flickable.ensureFocusVisible(this)
                                }
                            }

                            Keys.onUpPressed: function(event) {
                                if (!popup.visible) {
                                    if (libraryDelegate.index > 0) {
                                        libraryProfilesRepeater.itemAt(libraryDelegate.index - 1).profileCombo.forceActiveFocus()
                                    } else {
                                        libraryProfilesToggle.forceActiveFocus()
                                    }
                                    event.accepted = true
                                }
                            }
                            Keys.onDownPressed: function(event) {
                                if (!popup.visible) {
                                    if (libraryDelegate.index < libraryProfilesRepeater.count - 1) {
                                        libraryProfilesRepeater.itemAt(libraryDelegate.index + 1).profileCombo.forceActiveFocus()
                                    }
                                    event.accepted = true
                                }
                            }
                            Keys.onReturnPressed: function(event) {
                                if (!popup.visible) {
                                    popup.open()
                                    event.accepted = true
                                }
                            }
                            Keys.onEnterPressed: function(event) {
                                if (!popup.visible) {
                                    popup.open()
                                    event.accepted = true
                                }
                            }

                            Component.onCompleted: {
                                refreshSelection()
                                initialized = true
                            }

                            onModelChanged: {
                                if (!initialized) return
                                refreshSelection()
                            }

                            onCurrentIndexChanged: {
                                if (!initialized || updatingSelection) return
                                if (currentIndex < 0 || currentIndex >= libraryDelegate.profileOptions.length) return
                                var selected = libraryDelegate.profileOptions[currentIndex]

                                if (currentIndex === 0) {
                                    ConfigManager.setLibraryProfile(libraryDelegate.itemId, "")
                                } else if (selected !== undefined && selected !== "") {
                                    ConfigManager.setLibraryProfile(libraryDelegate.itemId, selected)
                                }
                            }
                        }
                    }
                }

                // Empty state
                Text {
                    visible: libraryProfilesRepeater.count === 0
                    text: qsTr("Go to the home screen first to load your libraries.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    font.italic: true
                }
            }
        }
        }
    }
}
