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

    GlassPanel {
        anchors.fill: parent
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.panelPadding - Theme.spacingMedium
        spacing: 0

        Text {
            text: qsTr("MPV")
            font.pixelSize: Theme.fontSizeHeader
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightHeavy
            color: Theme.textPrimary
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.spacingLarge
            Layout.leftMargin: Theme.spacingMedium
        }

        Flickable {
            id: flickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: outerColumn.implicitHeight + 2 * Theme.spacingSmall
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            function ensureFocusVisible(item) {
                if (!item) return
                var mapped = item.mapToItem(outerColumn, 0, 0)
                var itemY = outerColumn.y + mapped.y
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
                id: outerColumn
                x: Theme.spacingMedium
                y: Theme.spacingSmall
                width: flickable.width - 2 * Theme.spacingMedium
                spacing: Theme.spacingLarge

                // ========================================
                // 1. Default Profile Selector
                // ========================================
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        spacing: Math.round(4 * Theme.layoutScale)
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Default Profile")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Profile used when no library or series override is set")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    SettingsComboBox {
                        id: defaultProfileCombo
                        model: root.profileNames
                        Layout.preferredWidth: Math.round(280 * Theme.layoutScale)
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

                        Keys.onDownPressed: function(event) {
                            if (!popup.visible) {
                                profileEditorToggle.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()

                        background: FieldBackground { control: defaultProfileCombo; implicitHeight: Theme.buttonHeightSmall }

                        contentItem: Text {
                            text: defaultProfileCombo.selectedText()
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                        }

                        delegate: ItemDelegate {
                            width: defaultProfileCombo.width
                            contentItem: Text {
                                text: defaultProfileCombo.optionTextAt(index)
                                color: highlighted ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: highlighted ? Theme.surfaceInteractiveHover : "transparent"
                                radius: Theme.radiusSmall
                            }
                            highlighted: ListView.isCurrentItem || defaultProfileCombo.highlightedIndex === index
                        }

                        popup: Popup {
                            y: defaultProfileCombo.height + 5
                            width: defaultProfileCombo.width
                            implicitHeight: contentItem.implicitHeight
                            padding: 1

                            onOpened: {
                                defaultProfilePopupList.currentIndex = defaultProfileCombo.highlightedIndex >= 0
                                    ? defaultProfileCombo.highlightedIndex
                                    : defaultProfileCombo.currentIndex
                                defaultProfilePopupList.forceActiveFocus()
                            }
                            onClosed: defaultProfileCombo.forceActiveFocus()

                            contentItem: ListView {
                                id: defaultProfilePopupList
                                clip: true
                                implicitHeight: contentHeight
                                model: defaultProfileCombo.popup.visible ? defaultProfileCombo.delegateModel : null
                                currentIndex: defaultProfileCombo.highlightedIndex >= 0
                                    ? defaultProfileCombo.highlightedIndex
                                    : defaultProfileCombo.currentIndex

                                ScrollIndicator.vertical: ScrollIndicator { }

                                Keys.onReturnPressed: {
                                    defaultProfileCombo.applySelection(currentIndex)
                                    defaultProfileCombo.popup.close()
                                }
                                Keys.onEnterPressed: {
                                    defaultProfileCombo.applySelection(currentIndex)
                                    defaultProfileCombo.popup.close()
                                }
                                Keys.onEscapePressed: defaultProfileCombo.popup.close()
                            }

                            background: DialogSurface { floating: true; radius: Theme.radiusMedium }
                        }
                    }
                }

            // ========================================
            // 2. Profile Editor Toggle
            // ========================================
            ActionButton {
                id: profileEditorToggle
                variant: "ghost"
                text: qsTr("Edit Profiles")
                iconGlyph: profileEditorToggle.expanded ? Icons.expandLess : Icons.expandMore
                property bool expanded: false

                Layout.alignment: Qt.AlignLeft
                Layout.topMargin: Theme.spacingSmall
                focusPolicy: Qt.StrongFocus

                onActiveFocusChanged: {
                    if (activeFocus) {
                        root._lastFocusedItem = this
                        flickable.ensureFocusVisible(this)
                    }
                }



                KeyNavigation.up: defaultProfileCombo
                KeyNavigation.down: profileEditorToggle.expanded ? editProfileCombo : libraryProfilesToggle

                onClicked: expanded = !expanded
                Keys.onReturnPressed: expanded = !expanded
                Keys.onEnterPressed: expanded = !expanded
            }

            // ========================================
            // 3. Profile Editor Content
            // ========================================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                visible: profileEditorToggle.expanded

                // Select Profile to Edit
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    Text {
                        text: qsTr("Select Profile to Edit")
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }

                    SettingsComboBox {
                        id: editProfileCombo
                        model: root.profileNames
                        Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
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

                        background: FieldBackground { control: editProfileCombo; implicitHeight: Theme.buttonHeightSmall }

                        contentItem: Text {
                            text: editProfileCombo.selectedText()
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                        }

                        delegate: ItemDelegate {
                            width: editProfileCombo.width
                            contentItem: Text {
                                text: editProfileCombo.optionTextAt(index)
                                color: highlighted ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: highlighted ? Theme.surfaceInteractiveHover : "transparent"
                                radius: Theme.radiusSmall
                            }
                            highlighted: ListView.isCurrentItem || editProfileCombo.highlightedIndex === index
                        }

                        popup: Popup {
                            y: editProfileCombo.height + 5
                            width: editProfileCombo.width
                            implicitHeight: contentItem.implicitHeight
                            padding: 1

                            onOpened: {
                                editProfilePopupList.currentIndex = editProfileCombo.highlightedIndex >= 0
                                    ? editProfileCombo.highlightedIndex
                                    : editProfileCombo.currentIndex
                                editProfilePopupList.forceActiveFocus()
                            }
                            onClosed: editProfileCombo.forceActiveFocus()

                            contentItem: ListView {
                                id: editProfilePopupList
                                clip: true
                                implicitHeight: contentHeight
                                model: editProfileCombo.popup.visible ? editProfileCombo.delegateModel : null
                                currentIndex: editProfileCombo.highlightedIndex >= 0
                                    ? editProfileCombo.highlightedIndex
                                    : editProfileCombo.currentIndex

                                ScrollIndicator.vertical: ScrollIndicator { }

                                Keys.onReturnPressed: {
                                    editProfileCombo.currentIndex = currentIndex
                                    editProfileCombo.popup.close()
                                }
                                Keys.onEnterPressed: {
                                    editProfileCombo.currentIndex = currentIndex
                                    editProfileCombo.popup.close()
                                }
                                Keys.onEscapePressed: editProfileCombo.popup.close()
                            }

                            background: DialogSurface { floating: true; radius: Theme.radiusMedium }
                        }
                    }
                }

                // Button Row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

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
            ActionButton {
                id: libraryProfilesToggle
                variant: "ghost"
                text: qsTr("Library Profiles")
                iconGlyph: libraryProfilesToggle.expanded ? Icons.expandLess : Icons.expandMore
                property bool expanded: false

                Layout.alignment: Qt.AlignLeft
                Layout.topMargin: Theme.spacingSmall
                focusPolicy: Qt.StrongFocus

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

                onClicked: expanded = !expanded
                Keys.onReturnPressed: expanded = !expanded
                Keys.onEnterPressed: expanded = !expanded
            }

            // ========================================
            // 5. Library Profiles Content
            // ========================================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
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

                    delegate: RowLayout {
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

                        Layout.fillWidth: true
                        spacing: Theme.spacingMedium

                        Text {
                            text: libraryDelegate.name
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            Layout.fillWidth: true
                            Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
                        }

                        SettingsComboBox {
                            id: libraryProfileCombo
                            model: libraryDelegate.profileOptions
                            Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
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

                            background: FieldBackground { control: libraryProfileCombo; implicitHeight: Theme.buttonHeightSmall }

                            contentItem: Text {
                                text: libraryProfileCombo.selectedText()
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.spacingSmall
                            }

                            delegate: ItemDelegate {
                                width: libraryProfileCombo.width
                                contentItem: Text {
                                    text: libraryProfileCombo.optionTextAt(index)
                                    color: highlighted ? Theme.textPrimary : Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: highlighted ? Theme.surfaceInteractiveHover : "transparent"
                                    radius: Theme.radiusSmall
                                }
                                highlighted: ListView.isCurrentItem || libraryProfileCombo.highlightedIndex === index
                            }

                            popup: Popup {
                                y: libraryProfileCombo.height + 5
                                width: libraryProfileCombo.width
                                implicitHeight: contentItem.implicitHeight
                                padding: 1

                                onOpened: {
                                    libraryProfilePopupList.currentIndex = libraryProfileCombo.highlightedIndex >= 0
                                        ? libraryProfileCombo.highlightedIndex
                                        : libraryProfileCombo.currentIndex
                                    libraryProfilePopupList.forceActiveFocus()
                                }
                                onClosed: libraryProfileCombo.forceActiveFocus()

                                contentItem: ListView {
                                    id: libraryProfilePopupList
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: libraryProfileCombo.popup.visible ? libraryProfileCombo.delegateModel : null
                                    currentIndex: libraryProfileCombo.highlightedIndex >= 0
                                        ? libraryProfileCombo.highlightedIndex
                                        : libraryProfileCombo.currentIndex

                                    ScrollIndicator.vertical: ScrollIndicator { }

                                    onCurrentIndexChanged: libraryProfileCombo.highlightedIndex = currentIndex

                                    Keys.onReturnPressed: {
                                        libraryProfileCombo.currentIndex = currentIndex
                                        libraryProfileCombo.popup.close()
                                    }
                                    Keys.onEnterPressed: {
                                        libraryProfileCombo.currentIndex = currentIndex
                                        libraryProfileCombo.popup.close()
                                    }
                                    Keys.onEscapePressed: libraryProfileCombo.popup.close()
                                }

                                background: DialogSurface { floating: true; radius: Theme.radiusMedium }
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
