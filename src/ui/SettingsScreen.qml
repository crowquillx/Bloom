import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BloomUI

/**
 * SettingsScreen - Application settings with glassmorphic design
 * 
 * Sections:
 * - Account: Server info, user info, sign out
 * - Playback: Completion threshold, autoplay settings
 * - Display: Backdrop rotation interval
 * - About: App version and info
 * 
 * All settings are persisted immediately via ConfigManager.
 */
FocusScope {
    id: root
    focus: true
    property string navigationId: "settings"
    property int contentMaxWidth: Math.max(1100, Math.min(width - Theme.paddingLarge * 2, Math.round(1600 * Theme.layoutScale)))

    // Home-style rotating backdrop state
    property var backdropCandidates: []
    property string currentBackdropUrl: ""
    
    // Cached MPV profile names to avoid model churn
    property var profileNames: []
    function updateProfileNames() {
        var names = ConfigManager.mpvProfileNames
        if (!names || names.length === undefined) names = []
        var filtered = names.filter(function(name) {
            return typeof name === "string" && name.length > 0 && name !== "Use Default"
        })
        // Deduplicate while preserving order
        var seen = {}
        var unique = []
        for (var i = 0; i < filtered.length; ++i) {
            var n = filtered[i]
            if (!seen[n]) {
                seen[n] = true
                unique.push(n)
            }
        }
        profileNames = unique
    }
    Connections {
        target: ConfigManager
        function onMpvProfilesChanged() { root.updateProfileNames() }
    }
    
    // Signal to request sign out (handled by Main.qml)
    signal signOutRequested()
    
    function openNewProfileDialog(returnFocusTarget) {
        newProfileDialog.restoreFocusTarget = returnFocusTarget || null
        newProfileDialog.open()
    }

    function openDeleteProfileDialog(profileName, returnFocusTarget) {
        deleteProfileDialog.profileToDelete = profileName || ""
        deleteProfileDialog.restoreFocusTarget = returnFocusTarget || null
        deleteProfileDialog.open()
    }
    
    // ========================================
    // Focus Management
    // ========================================

    function setAllSectionsExpanded(expanded) {
        accountSection.expanded = expanded
        playbackSection.expanded = expanded
        displaySection.expanded = expanded
        videoSection.expanded = expanded
        mpvProfilesSection.expanded = expanded
        metadataSection.expanded = expanded
        aboutSection.expanded = expanded
    }

    function refreshBackdropCandidates() {
        backdropCandidates = []
        currentBackdropUrl = ""
        LibraryService.getViews()
    }

    function addBackdropCandidate(item) {
        if (!item) return

        var itemId = item.Id
        var backdropTag = ""
        var backdropItemId = itemId

        if (item.BackdropImageTags && item.BackdropImageTags.length > 0) {
            backdropTag = item.BackdropImageTags[0]
        } else if (item.ParentBackdropImageTags && item.ParentBackdropImageTags.length > 0) {
            backdropTag = item.ParentBackdropImageTags[0]
            backdropItemId = item.ParentBackdropItemId || item.SeriesId || itemId
        }

        if (!backdropTag || !backdropItemId)
            return

        for (var i = 0; i < backdropCandidates.length; ++i) {
            if (backdropCandidates[i].itemId === backdropItemId && backdropCandidates[i].backdropTag === backdropTag)
                return
        }

        var newCandidates = backdropCandidates.slice()
        newCandidates.push({
            itemId: backdropItemId,
            backdropTag: backdropTag
        })
        backdropCandidates = newCandidates

        if (backdropCandidates.length === 1) {
            selectRandomBackdrop()
        }
    }

    function selectRandomBackdrop() {
        if (backdropCandidates.length === 0) {
            currentBackdropUrl = ""
            return
        }

        var randomIndex = Math.floor(Math.random() * backdropCandidates.length)
        var candidate = backdropCandidates[randomIndex]
        var url = LibraryService.getCachedImageUrlWithWidth(candidate.itemId, "Backdrop", 1920)
        if (url && candidate.backdropTag) {
            url += "?tag=" + candidate.backdropTag
        }
        currentBackdropUrl = url
    }
    
    Component.onCompleted: {
        updateProfileNames()
        refreshBackdropCandidates()
        // Start on first section header (sections are collapsed by default)
        accountSection.toggleButton.forceActiveFocus()
    }
    
    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            if (backdropCandidates.length === 0) {
                refreshBackdropCandidates()
            }
            accountSection.toggleButton.forceActiveFocus()
        }
    }

    Connections {
        target: LibraryService

        function onViewsLoaded(views) {
            if (!views || views.length === undefined)
                return
            var ids = []
            for (var i = 0; i < views.length; ++i) {
                ids.push(views[i].Id)
            }
            LibraryService.getNextUp()
            for (var j = 0; j < ids.length; ++j) {
                LibraryService.getLatestMedia(ids[j])
            }
        }

        function onNextUpLoaded(items) {
            if (!items || items.length === undefined)
                return
            for (var i = 0; i < items.length; ++i) {
                addBackdropCandidate(items[i])
            }
        }

        function onLatestMediaLoaded(parentId, items) {
            if (!items || items.length === undefined)
                return
            for (var i = 0; i < items.length; ++i) {
                addBackdropCandidate(items[i])
            }
        }
    }

    Timer {
        id: backdropRotationTimer
        interval: ConfigManager.backdropRotationInterval
        repeat: true
        running: backdropCandidates.length > 1 && !PlayerController.isPlaybackActive
        onTriggered: selectRandomBackdrop()
    }
    
    // ========================================
    // Background
    // ========================================
    
    Rectangle {
        anchors.fill: parent
        z: -3
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.backgroundPrimary }
            GradientStop { position: 1.0; color: Theme.backgroundSecondary }
        }
    }

    Rectangle {
        id: backdropContainer
        anchors.fill: parent
        z: -2
        color: "transparent"
        clip: true

        property bool showBackdrop1: true

        Image {
            id: backdrop1
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: parent.showBackdrop1 ? 1.0 : 0.0
            visible: true
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } enabled: Theme.uiAnimationsEnabled }

            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1.0
                blurMax: Theme.blurRadius
            }

            onStatusChanged: backdropContainer.checkStatus(this)
        }

        Image {
            id: backdrop2
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: parent.showBackdrop1 ? 0.0 : 1.0
            visible: true
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } enabled: Theme.uiAnimationsEnabled }

            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1.0
                blurMax: Theme.blurRadius
            }

            onStatusChanged: backdropContainer.checkStatus(this)
        }

        function checkStatus(img) {
            if (img.source.toString() !== root.currentBackdropUrl) return

            if (img.status === Image.Ready || (img.status === Image.Null && root.currentBackdropUrl === "")) {
                if (img === backdrop1) showBackdrop1 = true
                else showBackdrop1 = false
            }
        }

        Rectangle {
            anchors.fill: parent
            z: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.gradientOverlayStart }
                GradientStop { position: 0.35; color: Theme.gradientOverlayMiddle }
                GradientStop { position: 1.0; color: Theme.gradientOverlayEnd }
            }
        }
    }

    onCurrentBackdropUrlChanged: {
        var target = backdropContainer.showBackdrop1 ? backdrop2 : backdrop1
        if (target.source.toString() === currentBackdropUrl && target.status === Image.Ready) {
            if (target === backdrop1) backdropContainer.showBackdrop1 = true
            else backdropContainer.showBackdrop1 = false
            return
        }
        target.source = currentBackdropUrl
    }
    
    // ========================================
    // Dialogs
    // ========================================
    
    // New Profile Dialog
    Dialog {
        id: newProfileDialog
        title: qsTr("Create New Profile")
        modal: true
        anchors.centerIn: parent
        width: Math.round(560 * Theme.layoutScale)
        padding: Theme.spacingLarge
        property Item restoreFocusTarget: null
        
        background: Rectangle {
            color: Theme.cardBackground
            radius: Theme.radiusMedium
            border.color: Theme.cardBorder
            border.width: 1
        }
        
        header: Rectangle {
            color: "transparent"
            height: Math.round(68 * Theme.layoutScale)
            
            Text {
                text: newProfileDialog.title
                font.pixelSize: Theme.fontSizeTitle
                font.family: Theme.fontPrimary
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingLarge
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        
        contentItem: Item {
            implicitHeight: newProfileContent.implicitHeight + Theme.spacingSmall * 2
            implicitWidth: newProfileContent.implicitWidth + Theme.spacingSmall * 2

            ColumnLayout {
                id: newProfileContent
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                spacing: Theme.spacingMedium

                Text {
                    text: qsTr("Profile Name")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                }

                TextField {
                    id: newProfileNameField
                    Layout.fillWidth: true
                    placeholderText: "My Custom Profile"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary

                    Keys.onDownPressed: {
                        newProfileCancelBtn.forceActiveFocus()
                        event.accepted = true
                    }

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: Theme.inputBackground
                        border.color: newProfileNameField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: newProfileNameField.activeFocus ? 2 : 1
                    }
                }

                Text {
                    visible: newProfileNameField.text.trim() !== "" &&
                             ConfigManager.mpvProfileNames.indexOf(newProfileNameField.text.trim()) >= 0
                    text: qsTr("A profile with this name already exists")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: "#ff6b6b"
                }
            }
        }
        
        footer: Item {
            implicitHeight: Theme.buttonHeightSmall + Theme.spacingLarge * 2
            implicitWidth: parent ? parent.width : 400

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Button {
                    id: newProfileCancelBtn
                    Layout.fillWidth: true
                    text: "Cancel"
                    focusPolicy: Qt.StrongFocus

                    Keys.onUpPressed: {
                        newProfileNameField.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onRightPressed: {
                        if (newProfileCreateBtn.enabled) {
                            newProfileCreateBtn.forceActiveFocus()
                        }
                        event.accepted = true
                    }
                    Keys.onReturnPressed: newProfileDialog.reject()
                    Keys.onEnterPressed: newProfileDialog.reject()

                    onClicked: newProfileDialog.reject()

                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: newProfileCancelBtn.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground
                        border.color: newProfileCancelBtn.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: newProfileCancelBtn.activeFocus ? 2 : 1
                    }
                }

                Button {
                    id: newProfileCreateBtn
                    Layout.fillWidth: true
                    text: qsTr("Create")
                    focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                    enabled: newProfileNameField.text.trim() !== "" &&
                             ConfigManager.mpvProfileNames.indexOf(newProfileNameField.text.trim()) < 0

                    Keys.onUpPressed: {
                        newProfileNameField.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onLeftPressed: {
                        newProfileCancelBtn.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onReturnPressed: if (newProfileCreateBtn.enabled) newProfileDialog.accept()
                    Keys.onEnterPressed: if (newProfileCreateBtn.enabled) newProfileDialog.accept()
                    onEnabledChanged: {
                        if (!enabled && activeFocus) {
                            newProfileCancelBtn.forceActiveFocus()
                        }
                    }

                    onClicked: newProfileDialog.accept()

                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: parent.enabled ? Theme.textPrimary : Theme.textDisabled
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: parent.enabled ? (newProfileCreateBtn.hovered ? Theme.buttonPrimaryBackgroundHover : Theme.buttonPrimaryBackground)
                                             : Theme.buttonSecondaryBackground
                        border.color: newProfileCreateBtn.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: newProfileCreateBtn.activeFocus ? 2 : 1
                    }
                }
            }
        }

        onOpened: {
            Qt.callLater(function() {
                newProfileNameField.forceActiveFocus()
                newProfileNameField.selectAll()
            })
        }
        
        onAccepted: {
            var name = newProfileNameField.text.trim()
            if (name !== "") {
                // Create with default settings
                ConfigManager.setMpvProfile(name, {
                    "hwdecEnabled": true,
                    "hwdecMethod": "auto",
                    "deinterlace": false,
                    "deinterlaceMethod": "",
                    "videoOutput": "gpu-next",
                    "interpolation": false,
                    "extraArgs": ["--fullscreen"]
                })
                newProfileNameField.text = ""
                // Select the new profile
                editProfileCombo.currentIndex = ConfigManager.mpvProfileNames.indexOf(name)
            }
        }
        
        onRejected: {
            newProfileNameField.text = ""
        }

        onClosed: {
            Qt.callLater(function() {
                if (newProfileDialog.restoreFocusTarget) {
                    newProfileDialog.restoreFocusTarget.forceActiveFocus()
                } else {
                    newProfileBtn.forceActiveFocus()
                }
                newProfileDialog.restoreFocusTarget = null
            })
        }
    }
    
    // Delete Profile Confirmation Dialog
    Dialog {
        id: deleteProfileDialog
        title: qsTr("Delete Profile")
        modal: true
        anchors.centerIn: parent
        width: Math.round(560 * Theme.layoutScale)
        padding: Theme.spacingLarge
        
        property string profileToDelete: ""
        property Item restoreFocusTarget: null
        
        background: Rectangle {
            color: Theme.cardBackground
            radius: Theme.radiusMedium
            border.color: Theme.cardBorder
            border.width: 1
        }
        
        header: Rectangle {
            color: "transparent"
            height: Math.round(68 * Theme.layoutScale)
            
            Text {
                text: deleteProfileDialog.title
                font.pixelSize: Theme.fontSizeTitle
                font.family: Theme.fontPrimary
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingLarge
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        
        contentItem: Item {
            implicitHeight: deleteProfileContent.implicitHeight + Theme.spacingSmall * 2
            implicitWidth: deleteProfileContent.implicitWidth + Theme.spacingSmall * 2

            ColumnLayout {
                id: deleteProfileContent
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                spacing: Theme.spacingMedium

                Text {
                    text: qsTr("Are you sure you want to delete the profile \"%1\"?").arg(deleteProfileDialog.profileToDelete)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    text: qsTr("This will also remove any library or series assignments using this profile.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        
        footer: Item {
            implicitHeight: Theme.buttonHeightSmall + Theme.spacingLarge * 2
            implicitWidth: parent ? parent.width : 400

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Button {
                    id: deleteDialogCancelBtn
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    focusPolicy: Qt.StrongFocus

                    Keys.onRightPressed: {
                        deleteDialogConfirmBtn.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onReturnPressed: deleteProfileDialog.reject()
                    Keys.onEnterPressed: deleteProfileDialog.reject()

                    onClicked: deleteProfileDialog.reject()

                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: deleteDialogCancelBtn.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground
                        border.color: deleteDialogCancelBtn.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: deleteDialogCancelBtn.activeFocus ? 2 : 1
                    }
                }

                Button {
                    id: deleteDialogConfirmBtn
                    Layout.fillWidth: true
                    text: qsTr("Delete")
                    focusPolicy: Qt.StrongFocus

                    Keys.onLeftPressed: {
                        deleteDialogCancelBtn.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onReturnPressed: deleteProfileDialog.accept()
                    Keys.onEnterPressed: deleteProfileDialog.accept()

                    onClicked: deleteProfileDialog.accept()

                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: "#ff6b6b"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: deleteDialogConfirmBtn.hovered ? Qt.rgba(1, 0.4, 0.4, 0.2) : "transparent"
                        border.color: deleteDialogConfirmBtn.activeFocus ? Theme.focusBorder : "#ff6b6b"
                        border.width: deleteDialogConfirmBtn.activeFocus ? 2 : 1
                    }
                }
            }
        }

        onOpened: {
            Qt.callLater(function() {
                deleteDialogCancelBtn.forceActiveFocus()
            })
        }
        
        onAccepted: {
            if (ConfigManager.deleteMpvProfile(profileToDelete)) {
                // Select first profile after deletion
                editProfileCombo.currentIndex = 0
            }
            profileToDelete = ""
        }
        
        onRejected: {
            profileToDelete = ""
        }

        onClosed: {
            Qt.callLater(function() {
                var target = deleteProfileDialog.restoreFocusTarget
                if (target === deleteProfileBtn && !deleteProfileBtn.enabled) {
                    duplicateProfileBtn.forceActiveFocus()
                } else if (target) {
                    target.forceActiveFocus()
                } else {
                    duplicateProfileBtn.forceActiveFocus()
                }
                deleteProfileDialog.restoreFocusTarget = null
            })
        }
    }
    
    // ========================================
    // Main Content
    // ========================================
    
    ScrollView {
        id: scrollView
        anchors.fill: parent
        anchors.margins: Theme.paddingLarge
        contentWidth: availableWidth
        clip: true
        
        Flickable {
            id: flickable
            contentHeight: contentColumn.height
            boundsBehavior: Flickable.StopAtBounds
            
            // Scroll focused item into view
            function ensureFocusVisible(item) {
                if (!item) return
                var mapped = item.mapToItem(contentColumn, 0, 0)
                var itemY = mapped.y
                var itemHeight = item.height
                var viewTop = contentY
                var viewBottom = contentY + height
                
                // Add padding for better visibility
                var padding = 50
                
                if (itemY < viewTop + padding) {
                    contentY = Math.max(0, itemY - padding)
                } else if (itemY + itemHeight > viewBottom - padding) {
                    contentY = Math.min(contentHeight - height, itemY + itemHeight - height + padding)
                }
            }
            
            function scrollToBottom() {
                contentY = Math.max(0, contentHeight - height)
            }
            
            function scrollToTop() {
                contentY = 0
            }
            
            ColumnLayout {
                id: contentColumn
                width: Math.min(parent.width, root.contentMaxWidth)
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spacingLarge
                
                // ========================================
                // Header
                // ========================================
                
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Math.round(138 * Theme.layoutScale)
                    radius: Theme.radiusLarge
                    color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.78)
                    border.color: Theme.cardBorder
                    border.width: Theme.borderWidth

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingLarge
                        spacing: Theme.spacingMedium

                        Rectangle {
                            width: Math.round(64 * Theme.layoutScale)
                            height: Math.round(64 * Theme.layoutScale)
                            radius: Math.round(20 * Theme.layoutScale)
                            color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.16)
                            border.color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.5)
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: Icons.settings
                                font.family: Theme.fontIcon
                                font.pixelSize: Theme.fontSizeTitle
                                color: Theme.accentPrimary
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Math.round(4 * Theme.layoutScale)

                            Text {
                                text: qsTr("Settings")
                                font.pixelSize: Theme.fontSizeHeader
                                font.family: Theme.fontPrimary
                                font.weight: Font.Bold
                                color: Theme.textPrimary
                            }

                            Text {
                                text: qsTr("Configure playback, display, and account preferences")
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        RowLayout {
                            spacing: Theme.spacingSmall
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                            Button {
                                id: expandAllButton
                                text: qsTr("Expand All")
                                focusPolicy: Qt.StrongFocus
                                onClicked: root.setAllSectionsExpanded(true)
                                Keys.onReturnPressed: root.setAllSectionsExpanded(true)
                                Keys.onEnterPressed: root.setAllSectionsExpanded(true)
                                KeyNavigation.right: collapseAllButton
                                KeyNavigation.down: accountSection.toggleButton

                                contentItem: Text {
                                    text: parent.text
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    implicitWidth: Math.round(150 * Theme.layoutScale)
                                    implicitHeight: Theme.buttonHeightSmall
                                    radius: Theme.radiusSmall
                                    color: expandAllButton.activeFocus ? Theme.buttonSecondaryBackgroundHover
                                         : (expandAllButton.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground)
                                    border.color: expandAllButton.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
                                    border.width: expandAllButton.activeFocus ? 2 : Theme.buttonBorderWidth
                                }
                            }

                            Button {
                                id: collapseAllButton
                                text: qsTr("Collapse All")
                                focusPolicy: Qt.StrongFocus
                                onClicked: root.setAllSectionsExpanded(false)
                                Keys.onReturnPressed: root.setAllSectionsExpanded(false)
                                Keys.onEnterPressed: root.setAllSectionsExpanded(false)
                                KeyNavigation.left: expandAllButton
                                KeyNavigation.down: accountSection.toggleButton

                                contentItem: Text {
                                    text: parent.text
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    implicitWidth: Math.round(160 * Theme.layoutScale)
                                    implicitHeight: Theme.buttonHeightSmall
                                    radius: Theme.radiusSmall
                                    color: collapseAllButton.activeFocus ? Theme.buttonSecondaryBackgroundHover
                                         : (collapseAllButton.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground)
                                    border.color: collapseAllButton.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
                                    border.width: collapseAllButton.activeFocus ? 2 : Theme.buttonBorderWidth
                                }
                            }
                        }
                    }
                }
                
                // ========================================
                // Account Section
                // ========================================
                
                SettingsSection {
                    id: accountSection
                    title: qsTr("Account")
                    icon: Icons.account
                    expanded: false
                    previousSection: null
                    previousSectionButton: collapseAllButton
                    nextSectionButton: playbackSection.toggleButton
                    firstFocusableItem: signOutButton
                    lastFocusableItem: signOutButton
                    Layout.fillWidth: true
                    
                    ColumnLayout {
                        spacing: Theme.spacingMedium
                        Layout.fillWidth: true
                        
                        // Server URL
                        SettingsInfoRow {
                            label: qsTr("Server")
                            value: ConfigManager.serverUrl || qsTr("Not connected")
                            Layout.fillWidth: true
                        }
                        
                        // Username
                        SettingsInfoRow {
                            label: qsTr("User")
                            value: ConfigManager.username || qsTr("Unknown")
                            Layout.fillWidth: true
                        }
                        
                        // Sign Out Button
                        Button {
                            id: signOutButton
                            text: qsTr("Sign Out")
                            Accessible.role: Accessible.Button
                            Accessible.name: text
                            Accessible.description: qsTr("Press to sign out from the current server")
                            Layout.alignment: Qt.AlignLeft
                            Layout.topMargin: Theme.spacingSmall
                            
                            onActiveFocusChanged: {
                                if (activeFocus) flickable.ensureFocusVisible(this)
                            }
                            
                            contentItem: RowLayout {
                                spacing: Theme.spacingSmall
                                
                                Text {
                                    text: Icons.logout
                                    font.family: Theme.fontIcon
                                    font.pixelSize: Theme.fontSizeIcon
                                    color: signOutButton.activeFocus ? Theme.textPrimary : Theme.textSecondary
                                }
                                
                                Text {
                                    text: qsTr("Sign Out")
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: signOutButton.activeFocus ? Theme.textPrimary : Theme.textSecondary
                                }
                            }
                            
                            background: Rectangle {
                                implicitWidth: 160
                                implicitHeight: Theme.buttonHeightSmall
                                radius: Theme.radiusSmall
                                color: signOutButton.activeFocus ? Theme.buttonSecondaryBackgroundHover 
                                     : (signOutButton.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground)
                                border.color: signOutButton.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
                                border.width: signOutButton.activeFocus ? 2 : Theme.buttonBorderWidth
                                
                                Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                                Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                            }
                            
                            // Stop wrap at top; keep focus here and ensure view is at top
                            Keys.onUpPressed: {
                                accountSection.toggleButton.forceActiveFocus()
                                event.accepted = true
                            }
                            KeyNavigation.up: null
                            KeyNavigation.down: playbackSection.toggleButton
                            
                            onClicked: root.signOutRequested()
                            Keys.onReturnPressed: root.signOutRequested()
                            Keys.onEnterPressed: root.signOutRequested()
                        }
                    }
                }
                
                // ========================================
                // Playback Section
                // ========================================
                
                SettingsSection {
                    id: playbackSection
                    title: qsTr("Playback")
                    icon: Icons.playArrow
                    expanded: false
                    previousSection: accountSection
                    previousSectionButton: accountSection.toggleButton
                    nextSectionButton: displaySection.toggleButton
                    firstFocusableItem: autoplaySwitch
                    lastFocusableItem: themeSongLoopToggle
                    Layout.fillWidth: true
                    
                    ColumnLayout {
                        spacing: Theme.spacingLarge
                        Layout.fillWidth: true
                        
                        // Autoplay Next Episode
                        SettingsToggleRow {
                            id: autoplaySwitch
                            label: qsTr("Autoplay Next Episode")
                            description: qsTr("Automatically play the next episode when the current one ends")
                            checked: ConfigManager.autoplayNextEpisode
                            Layout.fillWidth: true
                            focus: true
                            
                            KeyNavigation.up: playbackSection.toggleButton
                            KeyNavigation.down: thresholdSlider
                            
                            onToggled: function(value) {
                                ConfigManager.autoplayNextEpisode = value
                            }
                        }
                        
                        // Completion Threshold
                        SettingsSliderRow {
                            id: thresholdSlider
                            label: qsTr("Mark as Watched")
                            description: qsTr("Percentage of playback required to mark as watched")
                            value: ConfigManager.playbackCompletionThreshold
                            from: 50
                            to: 100
                            stepSize: 5
                            unit: "%"
                            Layout.fillWidth: true
                            
                            KeyNavigation.up: autoplaySwitch
                            KeyNavigation.down: audioDelaySpinBox
                            
                            onSliderValueChanged: function(newValue) {
                                ConfigManager.playbackCompletionThreshold = newValue
                            }
                        }
                        
                        // Audio Delay
                        SettingsSpinBoxRow {
                            id: audioDelaySpinBox
                            label: qsTr("Audio Delay")
                            description: qsTr("Adjust audio synchronization (positive delays audio, negative advances it)")
                            value: ConfigManager.audioDelay
                            from: -5000
                            to: 5000
                            stepSize: 50
                            unit: "ms"
                            Layout.fillWidth: true
                            
                            KeyNavigation.up: thresholdSlider
                            KeyNavigation.down: skipPopupDurationSlider
                            
                            onSpinBoxValueChanged: function(newValue) {
                                ConfigManager.audioDelay = newValue
                            }
                        }

                        SettingsSliderRow {
                            id: skipPopupDurationSlider
                            label: qsTr("Skip Intro/Credits Popup Duration")
                            description: qsTr("How long the playback skip popup stays visible (0 disables popup)")
                            value: ConfigManager.skipButtonAutoHideSeconds
                            from: 0
                            to: 15
                            stepSize: 1
                            unit: "s"
                            Layout.fillWidth: true

                            KeyNavigation.up: audioDelaySpinBox
                            KeyNavigation.down: autoSkipIntroToggle

                            onSliderValueChanged: function(newValue) {
                                ConfigManager.skipButtonAutoHideSeconds = newValue
                            }
                        }

                        SettingsToggleRow {
                            id: autoSkipIntroToggle
                            label: qsTr("Auto Skip Intro")
                            description: qsTr("Automatically skip intro once when it first appears")
                            checked: ConfigManager.autoSkipIntro
                            Layout.fillWidth: true

                            KeyNavigation.up: skipPopupDurationSlider
                            KeyNavigation.down: autoSkipOutroToggle

                            onToggled: function(value) {
                                ConfigManager.autoSkipIntro = value
                            }
                        }

                        SettingsToggleRow {
                            id: autoSkipOutroToggle
                            label: qsTr("Auto Skip Credits")
                            description: qsTr("Automatically skip credits once when they first appear")
                            checked: ConfigManager.autoSkipOutro
                            Layout.fillWidth: true

                            KeyNavigation.up: autoSkipIntroToggle
                            KeyNavigation.down: uiSoundsToggle

                            onToggled: function(value) {
                                ConfigManager.autoSkipOutro = value
                            }
                        }

                        // UI Sounds Toggle
                        SettingsToggleRow {
                            id: uiSoundsToggle
                            label: qsTr("UI Sounds")
                            description: qsTr("Play a sound on navigation, select, and back actions")
                            checked: ConfigManager.uiSoundsEnabled
                            Layout.fillWidth: true

                            KeyNavigation.up: autoSkipOutroToggle
                            KeyNavigation.down: uiSoundsVolumeCombo

                            onToggled: function(value) {
                                ConfigManager.uiSoundsEnabled = value
                            }
                        }

                        // UI Sounds Volume
                        RowLayout {
                            id: uiSoundsVolumeRow
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium

                            ColumnLayout {
                                spacing: Math.round(4 * Theme.layoutScale)
                                Layout.fillWidth: true

                                Text {
                                    text: qsTr("UI Sound Volume")
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: Theme.textPrimary
                                }

                                Text {
                                    text: qsTr("Click feedback for navigation and selection")
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textSecondary
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }

                            ComboBox {
                                id: uiSoundsVolumeCombo
                                model: [qsTr("Off"), qsTr("Very Low"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                                currentIndex: ConfigManager.uiSoundsVolume
                                Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
                                focusPolicy: Qt.StrongFocus
                                enabled: ConfigManager.uiSoundsEnabled

                                onActiveFocusChanged: {
                                    if (activeFocus) flickable.ensureFocusVisible(this)
                                }

                                onCurrentIndexChanged: {
                                    if (currentIndex !== ConfigManager.uiSoundsVolume) {
                                        ConfigManager.uiSoundsVolume = currentIndex
                                    }
                                }

                                Keys.onUpPressed: function(event) {
                                    if (!popup.visible) {
                                        uiSoundsToggle.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }
                                Keys.onDownPressed: function(event) {
                                    if (!popup.visible) {
                                        themeSongVolumeCombo.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }

                                Keys.onReturnPressed: popup.open()
                                Keys.onEnterPressed: popup.open()

                                background: Rectangle {
                                    implicitHeight: Theme.buttonHeightSmall
                                    radius: Theme.radiusSmall
                                    color: Theme.inputBackground
                                    border.color: uiSoundsVolumeCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                    border.width: uiSoundsVolumeCombo.activeFocus ? 2 : 1
                                    opacity: uiSoundsVolumeCombo.enabled ? 1.0 : 0.5
                                }

                                contentItem: Text {
                                    text: uiSoundsVolumeCombo.displayText
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: uiSoundsVolumeCombo.enabled ? Theme.textPrimary : Theme.textDisabled
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: Theme.spacingSmall
                                }

                                delegate: ItemDelegate {
                                    width: uiSoundsVolumeCombo.width
                                    enabled: uiSoundsVolumeCombo.enabled
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
                                    highlighted: ListView.isCurrentItem || uiSoundsVolumeCombo.highlightedIndex === index
                                }

                                popup: Popup {
                                    y: uiSoundsVolumeCombo.height + 5
                                    width: uiSoundsVolumeCombo.width
                                    implicitHeight: contentItem.implicitHeight
                                    padding: 1

                                    onOpened: {
                                        uiSoundsVolumeList.currentIndex = uiSoundsVolumeCombo.highlightedIndex >= 0
                                            ? uiSoundsVolumeCombo.highlightedIndex
                                            : uiSoundsVolumeCombo.currentIndex
                                        uiSoundsVolumeList.forceActiveFocus()
                                    }
                                    onClosed: uiSoundsVolumeCombo.forceActiveFocus()

                                    contentItem: ListView {
                                        id: uiSoundsVolumeList
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: uiSoundsVolumeCombo.popup.visible ? uiSoundsVolumeCombo.delegateModel : null
                                        currentIndex: uiSoundsVolumeCombo.highlightedIndex >= 0
                                            ? uiSoundsVolumeCombo.highlightedIndex
                                            : uiSoundsVolumeCombo.currentIndex

                                        ScrollIndicator.vertical: ScrollIndicator { }

                                        onCurrentIndexChanged: uiSoundsVolumeCombo.highlightedIndex = currentIndex

                                        Keys.onReturnPressed: {
                                            uiSoundsVolumeCombo.currentIndex = currentIndex
                                            uiSoundsVolumeCombo.popup.close()
                                        }
                                        Keys.onEnterPressed: {
                                            uiSoundsVolumeCombo.currentIndex = currentIndex
                                            uiSoundsVolumeCombo.popup.close()
                                        }
                                        Keys.onEscapePressed: uiSoundsVolumeCombo.popup.close()
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
                        
                        // Theme Song Volume
                        RowLayout {
                            id: themeSongVolumeRow
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium
                            
                            ColumnLayout {
                                spacing: Math.round(4 * Theme.layoutScale)
                                Layout.fillWidth: true
                                
                                Text {
                                    text: qsTr("Theme Song Volume")
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: Theme.textPrimary
                                }
                                
                                Text {
                                    text: qsTr("Play series theme music when browsing")
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textSecondary
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                            
                            ComboBox {
                                id: themeSongVolumeCombo
                                model: [qsTr("Off"), qsTr("Very Low"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                                currentIndex: ConfigManager.themeSongVolume
                                Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
                                focusPolicy: Qt.StrongFocus
                                
                                onActiveFocusChanged: {
                                    if (activeFocus) flickable.ensureFocusVisible(this)
                                }
                                
                                onCurrentIndexChanged: {
                                    if (currentIndex !== ConfigManager.themeSongVolume) {
                                        ConfigManager.themeSongVolume = currentIndex
                                    }
                                }

                                Keys.onUpPressed: function(event) {
                                    if (!popup.visible) {
                                        uiSoundsVolumeCombo.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }
                                Keys.onDownPressed: function(event) {
                                    if (!popup.visible) {
                                        themeSongLoopToggle.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }

                                Keys.onReturnPressed: popup.open()
                                Keys.onEnterPressed: popup.open()
                                
                                background: Rectangle {
                                    implicitHeight: Theme.buttonHeightSmall
                                    radius: Theme.radiusSmall
                                    color: Theme.inputBackground
                                    border.color: themeSongVolumeCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                    border.width: themeSongVolumeCombo.activeFocus ? 2 : 1
                                }
                                
                                contentItem: Text {
                                    text: themeSongVolumeCombo.displayText
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: Theme.textPrimary
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: Theme.spacingSmall
                                }

                                delegate: ItemDelegate {
                                    width: themeSongVolumeCombo.width
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
                                    highlighted: ListView.isCurrentItem || themeSongVolumeCombo.highlightedIndex === index
                                }

                                popup: Popup {
                                    y: themeSongVolumeCombo.height + 5
                                    width: themeSongVolumeCombo.width
                                    implicitHeight: contentItem.implicitHeight
                                    padding: 1
                                    
                                    onOpened: {
                                        themeSongVolumeList.currentIndex = themeSongVolumeCombo.highlightedIndex >= 0
                                            ? themeSongVolumeCombo.highlightedIndex
                                            : themeSongVolumeCombo.currentIndex
                                        themeSongVolumeList.forceActiveFocus()
                                    }
                                    onClosed: themeSongVolumeCombo.forceActiveFocus()

                                    contentItem: ListView {
                                        id: themeSongVolumeList
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: themeSongVolumeCombo.popup.visible ? themeSongVolumeCombo.delegateModel : null
                                        currentIndex: themeSongVolumeCombo.highlightedIndex >= 0
                                            ? themeSongVolumeCombo.highlightedIndex
                                            : themeSongVolumeCombo.currentIndex

                                        ScrollIndicator.vertical: ScrollIndicator { }

                                        onCurrentIndexChanged: themeSongVolumeCombo.highlightedIndex = currentIndex
                                        
                                        Keys.onReturnPressed: { 
                                            themeSongVolumeCombo.currentIndex = currentIndex
                                            themeSongVolumeCombo.popup.close() 
                                        }
                                        Keys.onEnterPressed: { 
                                            themeSongVolumeCombo.currentIndex = currentIndex
                                            themeSongVolumeCombo.popup.close() 
                                        }
                                        Keys.onEscapePressed: themeSongVolumeCombo.popup.close()
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
                        
                        // Theme Song Loop Toggle
                        SettingsToggleRow {
                            id: themeSongLoopToggle
                            label: qsTr("Loop Theme Song")
                            description: qsTr("Loop theme music while browsing a series")
                            checked: ConfigManager.themeSongLoop
                            Layout.fillWidth: true
                            
                            KeyNavigation.up: themeSongVolumeCombo
                            KeyNavigation.down: displaySection.toggleButton
                            
                            onToggled: function(value) {
                                ConfigManager.themeSongLoop = value
                            }
                        }
                    }
                }
                
                // ========================================
                // Display Section
                // ========================================
                
                SettingsSection {
                    id: displaySection
                    title: qsTr("Display")
                    icon: Icons.palette
                    expanded: false
                    previousSection: playbackSection
                    previousSectionButton: playbackSection.toggleButton
                    nextSectionButton: videoSection.toggleButton
                    firstFocusableItem: themeCombo
                    lastFocusableItem: dpiScaleSlider
                    Layout.fillWidth: true
                    
                    ColumnLayout {
                        spacing: Theme.spacingLarge
                        Layout.fillWidth: true
                        
                        // Theme Selector
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
                            
                            ComboBox {
                                id: themeCombo
                                model: Theme.themeNames
                                currentIndex: model.indexOf(ConfigManager.theme || "Jellyfin")
                                Layout.preferredWidth: Math.round(200 * Theme.layoutScale)
                                focusPolicy: Qt.StrongFocus
                                
                                onActiveFocusChanged: {
                                    if (activeFocus) flickable.ensureFocusVisible(this)
                                }
                                
                                onCurrentTextChanged: {
                                    if (currentText !== "" && currentText !== ConfigManager.theme) {
                                        ConfigManager.theme = currentText
                                    }
                                }

                                // Custom navigation to prevent ComboBox from trapping arrow keys
                                Keys.onUpPressed: function(event) {
                                    if (!popup.visible) {
                                        displaySection.toggleButton.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }
                                Keys.onDownPressed: function(event) {
                                    if (!popup.visible) {
                                        backdropSlider.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }

                                // Keyboard handling for opening popup
                                Keys.onReturnPressed: popup.open()
                                Keys.onEnterPressed: popup.open()
                                
                                background: Rectangle {
                                    implicitHeight: Theme.buttonHeightSmall
                                    radius: Theme.radiusSmall
                                    color: Theme.inputBackground
                                    border.color: themeCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                    border.width: themeCombo.activeFocus ? 2 : 1
                                }
                                
                                contentItem: Text {
                                    text: themeCombo.displayText
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: Theme.textPrimary
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: Theme.spacingSmall
                                }

                                delegate: ItemDelegate {
                                    width: themeCombo.width
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
                                    highlighted: ListView.isCurrentItem || themeCombo.highlightedIndex === index
                                }

                                popup: Popup {
                                    y: themeCombo.height + 5
                                    width: themeCombo.width
                                    implicitHeight: contentItem.implicitHeight
                                    padding: 1
                                    
                                    onOpened: {
                                        themeList.currentIndex = themeCombo.highlightedIndex >= 0
                                            ? themeCombo.highlightedIndex
                                            : themeCombo.currentIndex
                                        themeList.forceActiveFocus()
                                    }
                                    onClosed: themeCombo.forceActiveFocus()

                                    contentItem: ListView {
                                        id: themeList
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: themeCombo.popup.visible ? themeCombo.delegateModel : null
                                        currentIndex: themeCombo.highlightedIndex >= 0
                                            ? themeCombo.highlightedIndex
                                            : themeCombo.currentIndex

                                        ScrollIndicator.vertical: ScrollIndicator { }

                                        onCurrentIndexChanged: themeCombo.highlightedIndex = currentIndex
                                        
                                        Keys.onReturnPressed: { 
                                            themeCombo.currentIndex = currentIndex
                                            themeCombo.popup.close() 
                                        }
                                        Keys.onEnterPressed: { 
                                            themeCombo.currentIndex = currentIndex
                                            themeCombo.popup.close() 
                                        }
                                        Keys.onEscapePressed: themeCombo.popup.close()
                                    }

                                    background: Rectangle {
                                        color: Theme.cardBackground
                                        border.color: Theme.focusBorder
                                        border.width: 1
                                        radius: Theme.radiusSmall
                                    }
                                }
                                
                                KeyNavigation.up: themeSongLoopToggle
                                KeyNavigation.down: fullscreenToggle
                            }
                        }

                        // Launch in Fullscreen Toggle
                        SettingsToggleRow {
                            id: fullscreenToggle
                            label: qsTr("Launch in Fullscreen")
                            description: qsTr("Start the application in fullscreen mode")
                            checked: ConfigManager.launchInFullscreen
                            Layout.fillWidth: true

                            KeyNavigation.up: themeCombo
                            KeyNavigation.down: backdropSlider

                            onToggled: function(value) {
                                ConfigManager.launchInFullscreen = value
                            }
                        }

                        // Backdrop Rotation Interval
                        SettingsSliderRow {
                            id: backdropSlider
                            label: qsTr("Backdrop Rotation")
                            description: qsTr("How often the home screen backdrop changes")
                            value: ConfigManager.backdropRotationInterval / 1000
                            from: 10
                            to: 120
                            stepSize: 5
                            unit: "s"
                            Layout.fillWidth: true
                            
                            KeyNavigation.up: fullscreenToggle
                            KeyNavigation.down: dpiScaleSlider

                            onSliderValueChanged: function(newValue) {
                                ConfigManager.backdropRotationInterval = newValue * 1000
                            }
                        }

                        // DPI Scale Override Slider
                        SettingsSliderRow {
                            id: dpiScaleSlider
                            label: qsTr("Content Scale Override")
                            description: {
                                var overrideVal = ConfigManager.manualDpiScaleOverride
                                var effectiveVal = Theme.layoutScale
                                var autoScaleVal = (effectiveVal / (overrideVal || 1.0))
                                
                                var autoScale = autoScaleVal.toFixed(2)
                                var override = overrideVal.toFixed(2)
                                var effective = effectiveVal.toFixed(2)
                                return qsTr("Auto-detected: ") + autoScale + "x  |  Override: " + override + "x  |  Effective: " + effective + "x"
                            }
                            value: ConfigManager.manualDpiScaleOverride
                            from: 0.5
                            to: 2.0
                            stepSize: 0.1
                            unit: "x"
                            Layout.fillWidth: true
                            
                            KeyNavigation.up: backdropSlider
                            KeyNavigation.down: videoSection.toggleButton
                            
                            onSliderValueChanged: function(newValue) {
                                ConfigManager.manualDpiScaleOverride = newValue
                            }
                        }
                    }
                }

                // ========================================
                // Video Section
                // ========================================
                
                SettingsSection {
                    id: videoSection
                    title: qsTr("Video")
                    icon: Icons.videocam
                    expanded: false
                    previousSection: displaySection
                    previousSectionButton: displaySection.toggleButton
                    nextSectionButton: mpvProfilesSection.toggleButton
                    firstFocusableItem: framerateMatchingSwitch
                    lastFocusableItem: advancedToggle
                    Layout.fillWidth: true
                    
                    ColumnLayout {
                        spacing: Theme.spacingLarge
                        Layout.fillWidth: true
                        
                        // Framerate Matching Toggle
                        SettingsToggleRow {
                            id: framerateMatchingSwitch
                            label: qsTr("Enable Framerate Matching")
                            description: qsTr("Automatically adjust display refresh rate to match video content")
                            checked: ConfigManager.enableFramerateMatching
                            Layout.fillWidth: true
                            
                            KeyNavigation.up: videoSection.toggleButton
                            KeyNavigation.down: framerateDelaySlider
                            
                            onToggled: function(value) {
                                ConfigManager.enableFramerateMatching = value
                            }
                        }
                        
                        // Framerate Match Delay
                        SettingsSliderRow {
                            id: framerateDelaySlider
                            label: qsTr("Refresh Rate Switch Delay")
                            description: qsTr("Seconds to wait after switching refresh rate before starting playback (prevents dropped frames)")
                            value: ConfigManager.framerateMatchDelay
                            from: 0
                            to: 5
                            stepSize: 1
                            unit: "s"
                            Layout.fillWidth: true
                            enabled: ConfigManager.enableFramerateMatching
                            
                            KeyNavigation.up: framerateMatchingSwitch
                            KeyNavigation.down: hdrSwitch
                            
                            onSliderValueChanged: function(newValue) {
                                ConfigManager.framerateMatchDelay = newValue
                            }
                        }
                        
                        // HDR Toggle
                        SettingsToggleRow {
                            id: hdrSwitch
                            label: qsTr("Enable HDR")
                            description: qsTr("Enable High Dynamic Range for compatible displays")
                            checked: ConfigManager.enableHDR
                            Layout.fillWidth: true
                            
                            KeyNavigation.up: framerateDelaySlider
                            KeyNavigation.down: advancedToggle
                            
                            onToggled: function(value) {
                                ConfigManager.enableHDR = value
                            }
                        }
                        

                        // Advanced Section Toggle Button
                        Button {
                            id: advancedToggle
                            property bool advancedExpanded: false
                            
                            Layout.alignment: Qt.AlignLeft
                            Layout.topMargin: Theme.spacingSmall
                            
                            onActiveFocusChanged: {
                                if (activeFocus) flickable.ensureFocusVisible(this)
                            }
                            
                            contentItem: RowLayout {
                                spacing: Theme.spacingSmall
                                
                                Text {
                                    text: advancedToggle.advancedExpanded ? "▼" : "▶"
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: advancedToggle.activeFocus ? Theme.textPrimary : Theme.textSecondary
                                }
                                
                                Text {
                                    text: qsTr("Advanced")
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: advancedToggle.activeFocus ? Theme.textPrimary : Theme.textSecondary
                                }
                            }
                            
                            background: Rectangle {
                                implicitWidth: 140
                                implicitHeight: Theme.buttonHeightSmall
                                radius: Theme.radiusSmall
                                color: advancedToggle.activeFocus ? Theme.buttonSecondaryBackgroundHover 
                                     : (advancedToggle.hovered ? Theme.buttonSecondaryBackgroundHover : "transparent")
                                border.color: advancedToggle.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
                                border.width: advancedToggle.activeFocus ? 2 : Theme.buttonBorderWidth
                                
                                Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                                Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                            }
                            
                            KeyNavigation.up: hdrSwitch
                            KeyNavigation.down: mpvProfilesSection.toggleButton
                            
                            onClicked: advancedExpanded = !advancedExpanded
                            Keys.onReturnPressed: advancedExpanded = !advancedExpanded
                            Keys.onEnterPressed: advancedExpanded = !advancedExpanded
                        }
                        
                        // Advanced Section Content
                        ColumnLayout {
                            spacing: Theme.spacingMedium
                            Layout.fillWidth: true
                            visible: advancedToggle.advancedExpanded
                            
                            SettingsTextInputRow {
                                label: qsTr("Linux Refresh Rate Command")
                                placeholderText: "xrandr --output HDMI-1 --rate {RATE}"
                                text: ConfigManager.linuxRefreshRateCommand
                                Layout.fillWidth: true
                                onTextEdited: function(newText) {
                                    ConfigManager.linuxRefreshRateCommand = newText
                                }
                            }
                            
                            SettingsTextInputRow {
                                label: qsTr("Linux HDR Command")
                                placeholderText: "(not commonly available)"
                                text: ConfigManager.linuxHDRCommand
                                Layout.fillWidth: true
                                onTextEdited: function(newText) {
                                    ConfigManager.linuxHDRCommand = newText
                                }
                            }
                            
                            SettingsTextInputRow {
                                label: qsTr("Windows Custom HDR Command")
                                placeholderText: "(uses native API by default)"
                                text: ConfigManager.windowsCustomHDRCommand
                                Layout.fillWidth: true
                                onTextEdited: function(newText) {
                                    ConfigManager.windowsCustomHDRCommand = newText
                                }
                            }
                            
                            // Help text
                            Text {
                                text: qsTr("Use {RATE} for exact refresh rate (e.g., 23.976) or {RATE_INT} for rounded integer (e.g., 24). Use {STATE} for on/off in HDR commands.")
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                Layout.topMargin: -Theme.spacingSmall
                            }
                        }
                    }
                }
                
                // ========================================
                // MPV Profiles Section
                // ========================================
                
                SettingsSection {
                    id: mpvProfilesSection
                    title: qsTr("MPV Profiles")
                    icon: Icons.tune
                    expanded: false
                    previousSection: videoSection
                    previousSectionButton: videoSection.toggleButton
                    nextSectionButton: metadataSection.toggleButton
                    firstFocusableItem: defaultProfileCombo
                    lastFocusableItem: libraryProfilesToggle
                    focusBottomHandler: function() {
                        if (libraryProfilesToggle.expanded && libraryProfilesRepeater.count > 0) {
                            libraryProfilesRepeater.itemAt(libraryProfilesRepeater.count - 1).children[1].forceActiveFocus()
                            return
                        }
                        if (profileEditorToggle.expanded && profileEditor) {
                            profileEditor.focusBottomAnchor()
                            return
                        }
                        libraryProfilesToggle.forceActiveFocus()
                    }
                    Layout.fillWidth: true
                    
                    ColumnLayout {
                        spacing: Theme.spacingLarge
                        Layout.fillWidth: true
                        
                        // Default Profile Selector
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
                                    if (activeFocus) flickable.ensureFocusVisible(this)
                                }
                                
                                // Only use KeyNavigation when popup is closed
                                Keys.onUpPressed: function(event) {
                                    if (!popup.visible) {
                                        mpvProfilesSection.toggleButton.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }
                                Keys.onDownPressed: function(event) {
                                    if (!popup.visible) {
                                        profileEditorToggle.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }

                                // Keyboard handling for opening popup
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
                                        profileList.currentIndex = defaultProfileCombo.highlightedIndex >= 0
                                            ? defaultProfileCombo.highlightedIndex
                                            : defaultProfileCombo.currentIndex
                                        profileList.forceActiveFocus()
                                    }
                                    onClosed: defaultProfileCombo.forceActiveFocus()

                                    contentItem: ListView {
                                        id: profileList
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: defaultProfileCombo.popup.visible ? defaultProfileCombo.delegateModel : null
                                        currentIndex: defaultProfileCombo.highlightedIndex >= 0
                                            ? defaultProfileCombo.highlightedIndex
                                            : defaultProfileCombo.currentIndex

                                        ScrollIndicator.vertical: ScrollIndicator { }

                                        onCurrentIndexChanged: defaultProfileCombo.highlightedIndex = currentIndex
                                        
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
                        
                        // Profile Editor Toggle
                        Button {
                            id: profileEditorToggle
                            property bool expanded: false
                            
                            Layout.alignment: Qt.AlignLeft
                            Layout.topMargin: Theme.spacingSmall
                            focusPolicy: Qt.StrongFocus
                            
                            onActiveFocusChanged: {
                                if (activeFocus) flickable.ensureFocusVisible(this)
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
                        
                        // Profile Editor Content
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium
                            visible: profileEditorToggle.expanded
                            
                            // Profile Selector
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
                                        if (activeFocus) flickable.ensureFocusVisible(this)
                                    }
                                    
                                    // Only use KeyNavigation when popup is closed
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
                                            editProfileList.currentIndex = editProfileCombo.highlightedIndex >= 0
                                                ? editProfileCombo.highlightedIndex
                                                : editProfileCombo.currentIndex
                                            editProfileList.forceActiveFocus()
                                        }
                                        onClosed: editProfileCombo.forceActiveFocus()

                                        contentItem: ListView {
                                            id: editProfileList
                                            clip: true
                                            implicitHeight: contentHeight
                                            model: editProfileCombo.popup.visible ? editProfileCombo.delegateModel : null
                                            currentIndex: editProfileCombo.highlightedIndex >= 0
                                                ? editProfileCombo.highlightedIndex
                                                : editProfileCombo.currentIndex

                                            ScrollIndicator.vertical: ScrollIndicator { }
                                            onCurrentIndexChanged: editProfileCombo.highlightedIndex = currentIndex

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
                                
                                Button {
                                    id: newProfileBtn
                                    text: qsTr("+ New Profile")
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("Create New Profile")
                                    focusPolicy: Qt.StrongFocus
                                    
                                    onActiveFocusChanged: {
                                        if (activeFocus) flickable.ensureFocusVisible(this)
                                    }
                                    
                                    Keys.onUpPressed: profileEditorToggle.forceActiveFocus()
                                    Keys.onDownPressed: profileEditor.forceActiveFocus()
                                    Keys.onLeftPressed: editProfileCombo.forceActiveFocus()
                                    Keys.onRightPressed: duplicateProfileBtn.forceActiveFocus()
                                    
                                    contentItem: Text {
                                        text: parent.text
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
                                        if (activeFocus) flickable.ensureFocusVisible(this)
                                    }

                                    Keys.onUpPressed: profileEditorToggle.forceActiveFocus()
                                    Keys.onDownPressed: profileEditor.forceActiveFocus()
                                    Keys.onLeftPressed: newProfileBtn.forceActiveFocus()
                                    Keys.onRightPressed: {
                                        if (deleteProfileBtn.enabled) {
                                            deleteProfileBtn.forceActiveFocus()
                                        }
                                        event.accepted = true
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
                                        text: parent.text
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
                                        if (activeFocus) flickable.ensureFocusVisible(this)
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
                                        text: parent.text
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
                                
                                onDeleteRequested: {
                                    root.openDeleteProfileDialog(profileName, profileEditor)
                                }
                                
                                onNavigateOut: function(direction) {
                                    if (direction === "up") {
                                        newProfileBtn.forceActiveFocus()
                                    } else if (direction === "down") {
                                        libraryProfilesToggle.forceActiveFocus()
                                    }
                                }
                            }
                        }
                        
                        // Library Profiles Toggle
                        Button {
                            id: libraryProfilesToggle
                            property bool expanded: false
                            
                            Layout.alignment: Qt.AlignLeft
                            Layout.topMargin: Theme.spacingSmall
                            focusPolicy: Qt.StrongFocus
                            
                            onActiveFocusChanged: {
                                if (activeFocus) flickable.ensureFocusVisible(this)
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
                            
                            KeyNavigation.up: null
                            KeyNavigation.down: libraryProfilesToggle.expanded && libraryProfilesRepeater.count > 0 ? libraryProfilesRepeater.itemAt(0).children[1] : null
                            
                            Keys.onUpPressed: {
                                if (profileEditorToggle.expanded && profileEditor) {
                                    profileEditor.focusBottomAnchor()
                                } else {
                                    profileEditorToggle.forceActiveFocus()
                                }
                                event.accepted = true
                            }

                            Keys.onDownPressed: {
                                if (libraryProfilesToggle.expanded && libraryProfilesRepeater.count > 0) {
                                    libraryProfilesRepeater.itemAt(0).children[1].forceActiveFocus()
                                } else {
                                    metadataSection.toggleButton.forceActiveFocus()
                                }
                                event.accepted = true
                            }
                            
                            onClicked: expanded = !expanded
                            Keys.onReturnPressed: expanded = !expanded
                            Keys.onEnterPressed: expanded = !expanded
                        }
                        
                        // Library Profiles Content
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
                            
                            // Library list from LibraryViewModel.views
                            Repeater {
                                id: libraryProfilesRepeater
                                model: LibraryViewModel.views
                                
                                delegate: RowLayout {
                                    id: libraryDelegate
                                    required property var modelData
                                    required property int index
                                    
                                    property string name: modelData.Name || ""
                                    property string itemId: modelData.Id || ""
            // Store profile options as a JS array so indexOf works reliably
            // and guard against mpvProfileNames being undefined on first load
            property var profileOptions: {
                var names = root.profileNames || []
                return [qsTr("Use Default")].concat(names)
            }
                                    
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingMedium
                                    
                                    Text {
                                        text: name
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
                                        
                                        // Flag to prevent saving during initialization
                                        property bool initialized: false
                                        // Guard to avoid saving while we re-sync selection when model changes
                                        property bool updatingSelection: false
                                        
                                        function refreshSelection() {
                                            if (!libraryDelegate.profileOptions || libraryDelegate.profileOptions.length === 0) {
                                                updatingSelection = true
                                                currentIndex = 0
                                                updatingSelection = false
                                                return
                                            }
                                            var assigned = ConfigManager.getLibraryProfile(itemId)
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
                                            if (activeFocus) flickable.ensureFocusVisible(this)
                                        }
                                        
                                        // Navigate between library items, only when popup is closed
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
                                                } else {
                                                    metadataSection.toggleButton.forceActiveFocus()
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
                                            // Mark as initialized after setting the correct index
                                            initialized = true
                                        }
                                        
                                        onModelChanged: {
                                            if (!initialized) return
                                            refreshSelection()
                                        }
                                        
                                        onCurrentIndexChanged: {
                                            // Only save after initialization is complete
                                            if (!initialized || updatingSelection) return
                                            if (currentIndex < 0 || currentIndex >= libraryDelegate.profileOptions.length) return
                                            var selected = libraryDelegate.profileOptions[currentIndex]
                                            
                                            if (currentIndex === 0) {
                                                ConfigManager.setLibraryProfile(itemId, "")
                                            } else if (selected !== undefined && selected !== "") {
                                                ConfigManager.setLibraryProfile(itemId, selected)
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
                                                libraryProfileList.currentIndex = libraryProfileCombo.highlightedIndex >= 0
                                                    ? libraryProfileCombo.highlightedIndex
                                                    : libraryProfileCombo.currentIndex
                                                libraryProfileList.forceActiveFocus()
                                            }
                                            onClosed: libraryProfileCombo.forceActiveFocus()
                                            
                                            contentItem: ListView {
                                                id: libraryProfileList
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
                            
                            // Hint to load libraries
                            Text {
                                visible: libraryProfilesRepeater.count === 0
                                text: "Go to the home screen first to load your libraries."
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                                font.italic: true
                            }
                        }
                    }
                }
                
                // ========================================
                // Metadata Providers
                // ========================================
                
                SettingsSection {
                    id: metadataSection
                    title: qsTr("Metadata Providers")
                    icon: Icons.cloud
                    expanded: false
                    previousSection: mpvProfilesSection
                    previousSectionButton: mpvProfilesSection.toggleButton
                    nextSectionButton: aboutSection.toggleButton
                    firstFocusableItem: mdbListApiKeyRow.input
                    lastFocusableItem: mdbListApiKeyRow.input
                    Layout.fillWidth: true
                    
                    ColumnLayout {
                        spacing: Theme.spacingMedium
                        Layout.fillWidth: true
                        
                        Text {
                            text: qsTr("MDBList")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }
                        
                        Text {
                            text: qsTr("Enter your MDBList API key to enable enhanced ratings (IMDb, Rotten Tomatoes, etc.) and additional metadata.")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        
                        SettingsTextInputRow {
                            id: mdbListApiKeyRow
                            label: qsTr("MDBList API Key")
                            text: ConfigManager.mdbListApiKey
                            placeholderText: qsTr("API Key")
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                            
                            Keys.onUpPressed: {
                                if (mpvProfilesSection.expanded && libraryProfilesToggle.expanded && libraryProfilesRepeater.count > 0) {
                                    libraryProfilesRepeater.itemAt(libraryProfilesRepeater.count - 1).children[1].forceActiveFocus()
                                } else {
                                    metadataSection.toggleButton.forceActiveFocus()
                                }
                            }
                            
                            onEditingFinished: {
                                ConfigManager.mdbListApiKey = text
                            }
                        }
                    }
                }
                
                // ========================================
                // About Section
                // ========================================
                
                SettingsSection {
                    id: aboutSection
                    title: qsTr("About")
                    icon: Icons.info
                    expanded: false
                    previousSection: metadataSection
                    previousSectionButton: metadataSection.toggleButton
                    nextSectionButton: null
                    firstFocusableItem: null
                    lastFocusableItem: null
                    Layout.fillWidth: true
                    
                    ColumnLayout {
                        spacing: Theme.spacingMedium
                        Layout.fillWidth: true
                        
                        SettingsInfoRow {
                            label: qsTr("Version")
                            value: appVersion
                            Layout.fillWidth: true
                        }
                        
                        SettingsInfoRow {
                            label: qsTr("Qt Version")
                            value: qtVersion
                            Layout.fillWidth: true
                        }
                        
                        Text {
                            text: qsTr("Bloom is a modern Jellyfin client designed for the 10-foot TV experience.")
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingSmall
                        }
                    }
                }
                
                // Bottom spacing
                Item {
                    Layout.preferredHeight: Theme.spacingXLarge
                }
            }
        }
    }
    
    // ========================================
    // Reusable Components
    // ========================================
    // Reusable Components
    
    // Settings Section Card
    component SettingsSection: Rectangle {
        id: sectionCard
        
        property string title: ""
        property string icon: ""
        property bool expanded: false
        property Item firstFocusableItem: null
        property Item lastFocusableItem: null
        property var focusBottomHandler: null
        property var previousSection: null
        property Item previousSectionButton: null
        property Item nextSectionButton: null
        property alias toggleButton: sectionHeaderButton
        default property alias content: sectionContent.data

        function focusBottomAnchor() {
            if (!expanded) {
                toggleButton.forceActiveFocus()
                return
            }
            if (typeof focusBottomHandler === "function") {
                focusBottomHandler()
                return
            }
            if (lastFocusableItem) {
                lastFocusableItem.forceActiveFocus()
                return
            }
            if (firstFocusableItem) {
                firstFocusableItem.forceActiveFocus()
                return
            }
            toggleButton.forceActiveFocus()
        }
        
        Accessible.role: Accessible.Grouping
        Accessible.name: title
        
        implicitHeight: sectionLayout.implicitHeight + Theme.spacingLarge * 2
        color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.76)
        radius: Theme.radiusLarge
        border.color: Theme.cardBorder
        border.width: Theme.borderWidth

        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.08) }
            GradientStop { position: 0.3; color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.78) }
            GradientStop { position: 1.0; color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.66) }
        }
        
        ColumnLayout {
            id: sectionLayout
            anchors.fill: parent
            anchors.margins: Theme.spacingLarge
            spacing: Theme.spacingMedium
            
            // Section Header
            Button {
                id: sectionHeaderButton
                Layout.fillWidth: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Button
                Accessible.name: sectionCard.title
                Accessible.description: sectionCard.expanded ? qsTr("Expanded") : qsTr("Collapsed")

                onActiveFocusChanged: {
                    if (activeFocus && typeof flickable !== "undefined") {
                        flickable.ensureFocusVisible(sectionCard)
                    }
                }

                onClicked: sectionCard.expanded = !sectionCard.expanded

                Keys.onSpacePressed: {
                    sectionCard.expanded = !sectionCard.expanded
                    event.accepted = true
                }
                Keys.onReturnPressed: {
                    sectionCard.expanded = !sectionCard.expanded
                    event.accepted = true
                }
                Keys.onEnterPressed: {
                    sectionCard.expanded = !sectionCard.expanded
                    event.accepted = true
                }
                Keys.onRightPressed: {
                    if (!sectionCard.expanded) {
                        sectionCard.expanded = true
                        event.accepted = true
                    }
                }
                Keys.onLeftPressed: {
                    if (sectionCard.expanded) {
                        sectionCard.expanded = false
                        event.accepted = true
                    }
                }
                Keys.onUpPressed: {
                    if (sectionCard.previousSection) {
                        sectionCard.previousSection.focusBottomAnchor()
                        event.accepted = true
                    } else if (sectionCard.previousSectionButton) {
                        sectionCard.previousSectionButton.forceActiveFocus()
                        event.accepted = true
                    }
                }
                Keys.onDownPressed: {
                    if (sectionCard.expanded && sectionCard.firstFocusableItem) {
                        sectionCard.firstFocusableItem.forceActiveFocus()
                        event.accepted = true
                    } else if (sectionCard.nextSectionButton) {
                        sectionCard.nextSectionButton.forceActiveFocus()
                        event.accepted = true
                    }
                }

                background: Rectangle {
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusMedium
                    color: sectionHeaderButton.activeFocus
                        ? Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.12)
                        : "transparent"
                    border.color: sectionHeaderButton.activeFocus ? Theme.focusBorder : "transparent"
                    border.width: sectionHeaderButton.activeFocus ? 2 : 0
                }

                spacing: Theme.spacingSmall
                contentItem: RowLayout {
                    spacing: Theme.spacingSmall

                    Text {
                        text: sectionCard.icon
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeTitle
                        color: Theme.accentPrimary
                    }

                    Text {
                        text: sectionCard.title
                        font.pixelSize: Theme.fontSizeTitle
                        font.family: Theme.fontPrimary
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        Layout.fillWidth: true
                    }

                    Text {
                        text: sectionCard.expanded ? Icons.expandLess : Icons.expandMore
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeTitle
                        color: Theme.textSecondary
                    }
                }
            }
            
            // Divider
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderLight
                visible: sectionCard.expanded
            }
            
            // Section Content
            ColumnLayout {
                id: sectionContent
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                visible: sectionCard.expanded
            }
        }
    }
    
    // Info Row (label: value)
    component SettingsInfoRow: RowLayout {
        property string label: ""
        property string value: ""
        
        Accessible.role: Accessible.StaticText
        Accessible.name: label + ": " + value
        
        spacing: Theme.spacingMedium
        
        Text {
            text: label
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            Layout.preferredWidth: 120
        }
        
        Text {
            text: value
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textPrimary
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
    }
    
    // Toggle Row (switch with label and description)
    component SettingsToggleRow: FocusScope {
        id: toggleRow
        
        property string label: ""
        property string description: ""
        property bool checked: false
        property bool isHovered: toggleHoverTracker.containsMouse
        property bool hasKeyboardFocus: toggleRow.activeFocus
        
        Accessible.role: Accessible.CheckBox
        Accessible.name: label
        Accessible.description: description
        Accessible.checkable: true
        Accessible.checked: checked
        
        signal toggled(bool value)
        
        implicitHeight: toggleContent.implicitHeight
        
        onActiveFocusChanged: {
            if (activeFocus) flickable.ensureFocusVisible(this)
        }
        
        Keys.onSpacePressed: toggle()
        Keys.onReturnPressed: toggle()
        Keys.onEnterPressed: toggle()
        
        function toggle() {
            checked = !checked
            toggled(checked)
        }
        
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            radius: Theme.radiusSmall
            color: toggleRow.hasKeyboardFocus
                ? Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.16)
                : (toggleRow.isHovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
            border.color: toggleRow.hasKeyboardFocus
                ? Theme.focusBorder
                : (toggleRow.isHovered ? Theme.borderLight : "transparent")
            border.width: toggleRow.hasKeyboardFocus ? 2 : (toggleRow.isHovered ? 1 : 0)

            Behavior on color { ColorAnimation { duration: Theme.durationShort } }
            Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
        }

        Rectangle {
            width: 4
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            radius: 2
            color: Theme.accentPrimary
            visible: toggleRow.hasKeyboardFocus
            opacity: visible ? 1.0 : 0.0

            Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }
        }
        
        RowLayout {
            id: toggleContent
            anchors.fill: parent
            spacing: Theme.spacingMedium
            
            ColumnLayout {
                spacing: Math.round(4 * Theme.layoutScale)
                Layout.fillWidth: true
                
                Text {
                    text: toggleRow.label
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                }
                
                Text {
                    text: toggleRow.description
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    visible: text.length > 0
                }
            }
            
            // Custom Toggle Switch
            Rectangle {
                id: switchTrack
                width: 52
                height: 28
                radius: 14
                color: toggleRow.checked ? Theme.accentPrimary : Qt.rgba(1, 1, 1, 0.2)
                
                Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                
                Rectangle {
                    id: switchThumb
                    width: 22
                    height: 22
                    radius: 11
                    x: toggleRow.checked ? parent.width - width - 3 : 3
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.textPrimary
                    
                    Behavior on x { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: toggleRow.toggle()
                }
            }
        }

        MouseArea {
            id: toggleHoverTracker
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: true
            z: 10
        }
    }
    
    // Slider Row (slider with label, description, and value display)
    component SettingsSliderRow: FocusScope {
        id: sliderRow
        
        property string label: ""
        property string description: ""
        property real value: 0
        property real from: 0
        property real to: 100
        property real stepSize: 1
        property string unit: ""
        
        Accessible.role: Accessible.Slider
        Accessible.name: label
        Accessible.description: description
        
        signal sliderValueChanged(real newValue)
        
        implicitHeight: sliderContent.implicitHeight
        
        onActiveFocusChanged: {
            if (activeFocus) flickable.ensureFocusVisible(this)
        }
        
        Keys.onLeftPressed: decreaseValue()
        Keys.onRightPressed: increaseValue()
        
        function decreaseValue() {
            var newVal = Math.max(from, value - stepSize)
            if (newVal !== value) {
                value = newVal
                sliderValueChanged(value)
            }
        }
        
        function increaseValue() {
            var newVal = Math.min(to, value + stepSize)
            if (newVal !== value) {
                value = newVal
                sliderValueChanged(value)
            }
        }
        
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            radius: Theme.radiusSmall
            color: sliderRow.activeFocus ? Theme.hoverOverlay : "transparent"
            border.color: sliderRow.activeFocus ? Theme.focusBorder : "transparent"
            border.width: sliderRow.activeFocus ? 2 : 0
        }
        
        ColumnLayout {
            id: sliderContent
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacingSmall
            
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                
                ColumnLayout {
                    spacing: Math.round(4 * Theme.layoutScale)
                    Layout.fillWidth: true
                    
                    Text {
                        text: sliderRow.label
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }
                    
                    Text {
                        text: sliderRow.description
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        visible: text.length > 0
                    }
                }
                
                // Value display
                Rectangle {
                    width: 60
                    height: 32
                    radius: Theme.radiusSmall
                    color: Theme.inputBackground
                    border.color: Theme.inputBorder
                    border.width: Theme.borderWidth
                    
                    Text {
                        anchors.centerIn: parent
                        text: Math.round(sliderRow.value) + sliderRow.unit
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }
                }
            }
            
            // Slider
            Slider {
                id: slider
                Layout.fillWidth: true
                from: sliderRow.from
                to: sliderRow.to
                stepSize: sliderRow.stepSize
                value: sliderRow.value
                
                onMoved: {
                    sliderRow.value = value
                    sliderRow.sliderValueChanged(value)
                }
                
                background: Rectangle {
                    x: slider.leftPadding
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    width: slider.availableWidth
                    height: 6
                    radius: 3
                    color: Qt.rgba(1, 1, 1, 0.2)
                    
                    Rectangle {
                        width: slider.visualPosition * parent.width
                        height: parent.height
                        radius: 3
                        color: Theme.accentPrimary
                    }
                }
                
                handle: Rectangle {
                    x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    width: 20
                    height: 20
                    radius: 10
                    color: slider.pressed ? Theme.accentPrimary : Theme.textPrimary
                    border.color: Theme.accentPrimary
                    border.width: 2
                    
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }
            }
        }
    }
    
    // Text Input Row (text field with label)
    component SettingsTextInputRow: ColumnLayout {
        id: textInputRow
        
        property alias input: textField
        property string label: ""
        property alias text: textField.text
        property alias placeholderText: textField.placeholderText
        property alias echoMode: textField.echoMode
        
        Accessible.role: Accessible.EditableText
        Accessible.name: label
        
        signal textEdited(string newText)
        signal editingFinished()
        
        spacing: Theme.spacingSmall
        
        Text {
            text: textInputRow.label
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textPrimary
            Layout.fillWidth: true
        }
        
        TextField {
            id: textField
            Layout.fillWidth: true
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            
            color: Theme.textPrimary
            placeholderTextColor: Theme.textSecondary
            
            // Ensure focus visibility
            onActiveFocusChanged: {
                if (activeFocus && typeof flickable !== "undefined") flickable.ensureFocusVisible(textInputRow)
            }
            
            background: Rectangle {
                implicitHeight: Theme.buttonHeightSmall
                radius: Theme.radiusSmall
                color: Theme.inputBackground
                border.color: textField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                border.width: textField.activeFocus ? 2 : Theme.borderWidth
                
                Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
            }
            
            onTextEdited: textInputRow.textEdited(text)
            onEditingFinished: textInputRow.editingFinished()
        }
    }
    
    // SpinBox Row (number input with label)
    component SettingsSpinBoxRow: FocusScope {
        id: spinBoxRow
        
        property string label: ""
        property string description: ""
        property int value: 0
        property int from: 0
        property int to: 100
        property int stepSize: 1
        property string unit: ""
        
        Accessible.role: Accessible.SpinButton
        Accessible.name: label
        Accessible.description: description
        
        signal spinBoxValueChanged(int newValue)
        
        implicitHeight: spinBoxContent.implicitHeight
        
        onActiveFocusChanged: {
            if (activeFocus) flickable.ensureFocusVisible(this)
        }
        
        Keys.onLeftPressed: spinBox.decrease()
        Keys.onRightPressed: spinBox.increase()
        
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            radius: Theme.radiusSmall
            color: spinBoxRow.activeFocus ? Theme.hoverOverlay : "transparent"
            border.color: spinBoxRow.activeFocus ? Theme.focusBorder : "transparent"
            border.width: spinBoxRow.activeFocus ? 2 : 0
        }
        
        ColumnLayout {
            id: spinBoxContent
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacingSmall
            
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                
                ColumnLayout {
                    spacing: Math.round(4 * Theme.layoutScale)
                    Layout.fillWidth: true
                    
                    Text {
                        text: spinBoxRow.label
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }
                    
                    Text {
                        text: spinBoxRow.description
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        visible: text.length > 0
                    }
                }
                
                SpinBox {
                    id: spinBox
                    from: spinBoxRow.from
                    to: spinBoxRow.to
                    stepSize: spinBoxRow.stepSize
                    value: spinBoxRow.value
                    editable: true
                    
                    onValueModified: {
                        spinBoxRow.value = value
                        spinBoxRow.spinBoxValueChanged(value)
                    }
                    
                    textFromValue: function(value, locale) {
                        return value + (spinBoxRow.unit ? " " + spinBoxRow.unit : "")
                    }

                    valueFromText: function(text, locale) {
                        var cleanText = text
                        if (spinBoxRow.unit) {
                            cleanText = text.replace(spinBoxRow.unit, "").trim()
                        }
                        return Number.fromLocaleString(locale, cleanText)
                    }
                    
                    contentItem: TextInput {
                        z: 2
                        text: spinBox.textFromValue(spinBox.value, spinBox.locale)
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                        selectionColor: Theme.accentPrimary
                        selectedTextColor: Theme.textPrimary
                        horizontalAlignment: Qt.AlignHCenter
                        verticalAlignment: Qt.AlignVCenter
                        readOnly: !spinBox.editable
                        validator: spinBox.validator
                        inputMethodHints: Qt.ImhDigitsOnly
                    }
                    
                    up.indicator: Rectangle {
                        x: spinBox.mirrored ? 0 : parent.width - width
                        height: parent.height
                        implicitWidth: 40
                        implicitHeight: 40
                        color: spinBox.up.pressed ? Theme.buttonPrimaryBackground : "transparent"
                        radius: Theme.radiusSmall
                        
                        Text {
                            text: "+"
                            font.pixelSize: Theme.fontSizeTitle
                            color: Theme.textPrimary
                            anchors.centerIn: parent
                        }
                    }
                    
                    down.indicator: Rectangle {
                        x: spinBox.mirrored ? parent.width - width : 0
                        height: parent.height
                        implicitWidth: 40
                        implicitHeight: 40
                        color: spinBox.down.pressed ? Theme.buttonPrimaryBackground : "transparent"
                        radius: Theme.radiusSmall
                        
                        Text {
                            text: "-"
                            font.pixelSize: Theme.fontSizeTitle
                            color: Theme.textPrimary
                            anchors.centerIn: parent
                        }
                    }
                    
                    background: Rectangle {
                        implicitWidth: 140
                        implicitHeight: Theme.buttonHeightSmall
                        color: Theme.inputBackground
                        border.color: Theme.inputBorder
                        border.width: 1
                        radius: Theme.radiusSmall
                    }
                }
            }
        }
    }
}
