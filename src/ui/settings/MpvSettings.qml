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
    signal openDeleteProfileDialog(string profileName, Item returnFocusTarget)

    function enterFromRail() {
        var target = _lastFocusedItem || defaultProfileCombo
        if (target) target.forceActiveFocus()
    }

    function restoreFocus() {
        enterFromRail()
    }

    Keys.priority: Keys.AfterItem
    Keys.onLeftPressed: function(event) { requestReturnToRail(); event.accepted = true }
    Keys.onEscapePressed: function(event) { requestReturnToRail(); event.accepted = true }

    implicitHeight: cardBackground.implicitHeight

    Rectangle {
        id: cardBackground
        anchors.fill: parent
        implicitHeight: outerColumn.implicitHeight + Theme.paddingLarge * 2
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

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.margins: Theme.paddingLarge
        contentHeight: outerColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        function ensureFocusVisible(item) {
            if (!item) return
            var mapped = item.mapToItem(outerColumn, 0, 0)
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
            id: outerColumn
            width: flickable.width
            spacing: Theme.spacingLarge

            // Section Title
            Text {
                text: qsTr("MPV")
                font.pixelSize: Theme.fontSizeLarge
                font.family: Theme.fontPrimary
                font.bold: true
                color: Theme.textPrimary
                Layout.fillWidth: true
            }

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

                ComboBox {
                    id: defaultProfileCombo
                    model: root.profileNames
                    Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
                    focusPolicy: Qt.StrongFocus

                    property bool initialized: false
                    property bool updatingSelection: false

                    function refreshSelection() {
                        var options = model || []
                        updatingSelection = true
                        var idx = options.indexOf(ConfigManager.defaultProfileName)
                        currentIndex = idx >= 0 ? idx : 0
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

                    onCurrentTextChanged: {
                        if (!initialized || updatingSelection) return
                        if (currentText && currentText !== ConfigManager.defaultProfileName) {
                            ConfigManager.defaultProfileName = currentText
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

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: Theme.inputBackground
                        border.color: defaultProfileCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: defaultProfileCombo.activeFocus ? 2 : 1
                    }

                    contentItem: Text {
                        text: defaultProfileCombo.displayText
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSmall
                    }

                    delegate: ItemDelegate {
                        width: defaultProfileCombo.width
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
                                defaultProfileCombo.currentIndex = currentIndex
                                defaultProfileCombo.popup.close()
                            }
                            Keys.onEnterPressed: {
                                defaultProfileCombo.currentIndex = currentIndex
                                defaultProfileCombo.popup.close()
                            }
                            Keys.onEscapePressed: defaultProfileCombo.popup.close()
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

            // ========================================
            // 2. Profile Editor Toggle
            // ========================================
            Button {
                id: profileEditorToggle
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

                contentItem: RowLayout {
                    spacing: Theme.spacingSmall

                    Text {
                        text: profileEditorToggle.expanded ? "▼" : "▶"
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeSmall
                        color: profileEditorToggle.activeFocus ? Theme.textPrimary : Theme.textSecondary
                    }

                    Text {
                        text: qsTr("Edit Profiles")
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: profileEditorToggle.activeFocus ? Theme.textPrimary : Theme.textSecondary
                    }
                }

                background: Rectangle {
                    implicitWidth: 160
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusSmall
                    color: profileEditorToggle.activeFocus ? Theme.buttonSecondaryBackgroundHover
                         : (profileEditorToggle.hovered ? Theme.buttonSecondaryBackgroundHover : "transparent")
                    border.color: profileEditorToggle.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
                    border.width: profileEditorToggle.activeFocus ? 2 : Theme.buttonBorderWidth
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

                    ComboBox {
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

                        background: Rectangle {
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: Theme.inputBackground
                            border.color: editProfileCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: editProfileCombo.activeFocus ? 2 : 1
                        }

                        contentItem: Text {
                            text: editProfileCombo.displayText
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                        }

                        delegate: ItemDelegate {
                            width: editProfileCombo.width
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
                    spacing: Theme.spacingMedium

                    Button {
                        id: newProfileBtn
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

                        contentItem: Text {
                            text: newProfileBtn.text
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.accentPrimary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            implicitWidth: 120
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: newProfileBtn.activeFocus || newProfileBtn.hovered ? Qt.rgba(0.4, 0.6, 1, 0.2) : "transparent"
                            border.color: newProfileBtn.activeFocus ? Theme.focusBorder : Theme.accentPrimary
                            border.width: newProfileBtn.activeFocus ? 2 : 1
                        }

                        onClicked: root.openNewProfileDialog(newProfileBtn)
                        Keys.onReturnPressed: root.openNewProfileDialog(newProfileBtn)
                        Keys.onEnterPressed: root.openNewProfileDialog(newProfileBtn)
                    }

                    Button {
                        id: duplicateProfileBtn
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
                            if (deleteProfileBtn.enabled) {
                                deleteProfileBtn.forceActiveFocus()
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

                        contentItem: Text {
                            text: duplicateProfileBtn.text
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: duplicateProfileBtn.enabled ? Theme.accentPrimary : Theme.textDisabled
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            implicitWidth: 130
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: duplicateProfileBtn.activeFocus || duplicateProfileBtn.hovered ? Qt.rgba(0.4, 0.6, 1, 0.2) : "transparent"
                            border.color: duplicateProfileBtn.activeFocus ? Theme.focusBorder : Theme.accentPrimary
                            border.width: duplicateProfileBtn.activeFocus ? 2 : 1
                            opacity: duplicateProfileBtn.enabled ? 1.0 : 0.5
                        }

                        onClicked: duplicateSelectedProfile()
                        Keys.onReturnPressed: duplicateSelectedProfile()
                        Keys.onEnterPressed: duplicateSelectedProfile()
                    }

                    Button {
                        id: deleteProfileBtn
                        text: qsTr("Delete")
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Delete selected profile")
                        focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                        enabled: {
                            var name = editProfileCombo.currentText || ""
                            return name !== "" && name !== "Default" && name !== "High Quality"
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

                        contentItem: Text {
                            text: deleteProfileBtn.text
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: deleteProfileBtn.enabled ? "#ff8c8c" : Theme.textDisabled
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            implicitWidth: 110
                            implicitHeight: Theme.buttonHeightSmall
                            radius: Theme.radiusSmall
                            color: deleteProfileBtn.activeFocus || deleteProfileBtn.hovered ? Qt.rgba(1, 0.4, 0.4, 0.2) : "transparent"
                            border.color: deleteProfileBtn.activeFocus ? Theme.focusBorder : "#ff8c8c"
                            border.width: deleteProfileBtn.activeFocus ? 2 : 1
                            opacity: deleteProfileBtn.enabled ? 1.0 : 0.5
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
            Button {
                id: libraryProfilesToggle
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

                contentItem: RowLayout {
                    spacing: Theme.spacingSmall

                    Text {
                        text: libraryProfilesToggle.expanded ? "▼" : "▶"
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeSmall
                        color: libraryProfilesToggle.activeFocus ? Theme.textPrimary : Theme.textSecondary
                    }

                    Text {
                        text: qsTr("Library Profiles")
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: libraryProfilesToggle.activeFocus ? Theme.textPrimary : Theme.textSecondary
                    }
                }

                background: Rectangle {
                    implicitWidth: 180
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusSmall
                    color: libraryProfilesToggle.activeFocus ? Theme.buttonSecondaryBackgroundHover
                         : (libraryProfilesToggle.hovered ? Theme.buttonSecondaryBackgroundHover : "transparent")
                    border.color: libraryProfilesToggle.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
                    border.width: libraryProfilesToggle.activeFocus ? 2 : Theme.buttonBorderWidth
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
                        libraryProfilesRepeater.itemAt(0).children[1].forceActiveFocus()
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

                        property string name: modelData.Name || ""
                        property string itemId: modelData.Id || ""
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

                        ComboBox {
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
                                        libraryProfilesRepeater.itemAt(libraryDelegate.index - 1).children[1].forceActiveFocus()
                                    } else {
                                        libraryProfilesToggle.forceActiveFocus()
                                    }
                                    event.accepted = true
                                }
                            }
                            Keys.onDownPressed: function(event) {
                                if (!popup.visible) {
                                    if (libraryDelegate.index < libraryProfilesRepeater.count - 1) {
                                        libraryProfilesRepeater.itemAt(libraryDelegate.index + 1).children[1].forceActiveFocus()
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

                            background: Rectangle {
                                implicitHeight: Theme.buttonHeightSmall
                                radius: Theme.radiusSmall
                                color: Theme.inputBackground
                                border.color: libraryProfileCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                border.width: libraryProfileCombo.activeFocus ? 2 : 1
                            }

                            contentItem: Text {
                                text: libraryProfileCombo.displayText
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.spacingSmall
                            }

                            delegate: ItemDelegate {
                                width: libraryProfileCombo.width
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

                                background: Rectangle {
                                    color: Theme.cardBackground
                                    border.color: Theme.focusBorder
                                    border.width: 1
                                    radius: Theme.radiusSmall
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
