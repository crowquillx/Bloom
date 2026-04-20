import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BloomUI
import "settings"

/**
 * SettingsScreen — Left-rail + content-panel orchestrator.
 *
 * Layout: narrow left rail (section list) + wide right content panel.
 * Each section is a standalone FocusScope inside a StackLayout.
 * All settings are persisted immediately via ConfigManager.
 */
FocusScope {
    id: root
    focus: true
    property string navigationId: "settings"
    property bool focusUpdatesOnActivate: false

    // Rotating backdrop state
    property var backdropCandidates: []
    property string currentBackdropUrl: ""

    // Cached MPV profile names (shared with MpvSettings)
    property var profileNames: []
    function updateProfileNames() {
        var names = ConfigManager.mpvProfileNames
        if (!names || names.length === undefined) names = []
        var filtered = names.filter(function(name) {
            return typeof name === "string" && name.length > 0 && name !== "Use Default"
        })
        var seen = {}
        var unique = []
        for (var i = 0; i < filtered.length; ++i) {
            var n = filtered[i]
            if (!seen[n]) { seen[n] = true; unique.push(n) }
        }
        profileNames = unique
    }
    Connections {
        target: ConfigManager
        function onMpvProfilesChanged() { root.updateProfileNames() }
    }

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

    function findSectionIndex(sectionName) {
        var sections = settingsRail.sectionModel || []
        for (var i = 0; i < sections.length; ++i) {
            if (sections[i].name === sectionName) return i
        }
        return 0
    }

    // Navigate to the About & Account section and focus updates controls.
    // Called by Main.qml when navigating to settings via "updates" sidebar item.
    function requestUpdateSectionFocus() {
        settingsRail.currentSection = findSectionIndex(qsTr("About & Account"))
        Qt.callLater(function() {
            aboutAccountSection.enterFromRail()
        })
    }

    function enterContentPanel() {
        var section = currentSectionItem()
        if (section && typeof section.enterFromRail === "function") {
            section.enterFromRail()
        }
    }

    function returnToRail() {
        settingsRail.focusRail()
    }

    function currentSectionItem() {
        switch (contentStack.currentIndex) {
        case 0: return playbackSection
        case 1: return displaySection
        case 2: return videoSection
        case 3: return mpvSection
        case 4: return integrationsSection
        case 5: return aboutAccountSection
        default: return null
        }
    }

    Component.onCompleted: {
        updateProfileNames()
        refreshBackdropCandidates()
        settingsRail.focusRail()
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            if (backdropCandidates.length === 0)
                refreshBackdropCandidates()
            if (focusUpdatesOnActivate) {
                requestUpdateSectionFocus()
                focusUpdatesOnActivate = false
            } else {
                settingsRail.focusRail()
            }
        }
    }

    // ========================================
    // Backdrop
    // ========================================

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
        if (!backdropTag || !backdropItemId) return
        for (var i = 0; i < backdropCandidates.length; ++i) {
            if (backdropCandidates[i].itemId === backdropItemId && backdropCandidates[i].backdropTag === backdropTag)
                return
        }
        var newCandidates = backdropCandidates.slice()
        newCandidates.push({ itemId: backdropItemId, backdropTag: backdropTag })
        backdropCandidates = newCandidates
        if (backdropCandidates.length === 1) selectRandomBackdrop()
    }

    function selectRandomBackdrop() {
        if (backdropCandidates.length === 0) { currentBackdropUrl = ""; return }
        var randomIndex = Math.floor(Math.random() * backdropCandidates.length)
        var candidate = backdropCandidates[randomIndex]
        var url = LibraryService.getCachedImageUrlWithWidth(candidate.itemId, "Backdrop", 1920)
        if (url && candidate.backdropTag) url += "?tag=" + candidate.backdropTag
        currentBackdropUrl = url
    }

    Connections {
        target: LibraryService
        function onViewsLoaded(views) {
            if (!views || views.length === undefined) return
            var ids = []
            for (var i = 0; i < views.length; ++i) ids.push(views[i].Id)
            LibraryService.getNextUp()
            for (var j = 0; j < ids.length; ++j) LibraryService.getLatestMedia(ids[j])
        }
        function onNextUpLoaded(items) {
            if (!items || items.length === undefined) return
            for (var i = 0; i < items.length; ++i) addBackdropCandidate(items[i])
        }
        function onLatestMediaLoaded(parentId, items) {
            if (!items || items.length === undefined) return
            for (var i = 0; i < items.length; ++i) addBackdropCandidate(items[i])
        }
    }

    Timer {
        id: backdropRotationTimer
        interval: ConfigManager.backdropRotationInterval
        repeat: true
        running: backdropCandidates.length > 1 && !PlayerController.isPlaybackActive
        onTriggered: selectRandomBackdrop()
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
    // Background layers
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
            asynchronous: true; cache: true
            opacity: parent.showBackdrop1 ? 1.0 : 0.0
            visible: true
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } enabled: Theme.uiAnimationsEnabled }
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true; blur: 1.0
                blurMax: Math.round(Theme.blurRadius * 1.5)
            }
            onStatusChanged: backdropContainer.checkStatus(this)
        }

        Image {
            id: backdrop2
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; cache: true
            opacity: parent.showBackdrop1 ? 0.0 : 1.0
            visible: true
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } enabled: Theme.uiAnimationsEnabled }
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true; blur: 1.0
                blurMax: Math.round(Theme.blurRadius * 1.5)
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

        // Stronger gradient scrim over the backdrop
        Rectangle {
            anchors.fill: parent
            z: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.60) }
                GradientStop { position: 0.35; color: Qt.rgba(0, 0, 0, 0.70) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.80) }
            }
        }
    }

    // ========================================
    // Dialogs
    // ========================================

    NewProfileDialog {
        id: newProfileDialog
        onProfileCreated: function(name) {
            ConfigManager.setMpvProfile(name, {
                "hwdecEnabled": true,
                "hwdecMethod": "auto",
                "deinterlace": false,
                "deinterlaceMethod": "",
                "videoOutput": "gpu-next",
                "interpolation": false,
                "extraArgs": ["--fullscreen"]
            })
        }
    }

    DeleteProfileDialog {
        id: deleteProfileDialog
        onProfileDeleted: function(name) {
            ConfigManager.deleteMpvProfile(name)
        }
    }

    // ========================================
    // Two-panel layout: Rail + Content
    // ========================================

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingLarge
        spacing: Theme.spacingLarge

        // Left rail
        SettingsRail {
            id: settingsRail
            Layout.fillHeight: true
            Layout.preferredWidth: implicitWidth

            onEnterContentRequested: root.enterContentPanel()
        }

        // Right content panel
        StackLayout {
            id: contentStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: settingsRail.currentSection

            PlaybackSettings {
                id: playbackSection
                onRequestReturnToRail: root.returnToRail()
            }

            DisplaySettings {
                id: displaySection
                onRequestReturnToRail: root.returnToRail()
            }

            VideoSettings {
                id: videoSection
                onRequestReturnToRail: root.returnToRail()
            }

            MpvSettings {
                id: mpvSection
                profileNames: root.profileNames
                onRequestReturnToRail: root.returnToRail()
                onOpenNewProfileDialog: function(returnTarget) { root.openNewProfileDialog(returnTarget) }
                onOpenDeleteProfileDialog: function(name, returnTarget) { root.openDeleteProfileDialog(name, returnTarget) }
            }

            IntegrationsSettings {
                id: integrationsSection
                onRequestReturnToRail: root.returnToRail()
            }

            AboutAccountSettings {
                id: aboutAccountSection
                appVersion: typeof appVersion !== "undefined" ? appVersion : ""
                appBuildChannel: typeof appBuildChannel !== "undefined" ? appBuildChannel : ""
                appBuildId: typeof appBuildId !== "undefined" ? appBuildId : ""
                qtVersion: typeof qtVersion !== "undefined" ? qtVersion : ""
                onRequestReturnToRail: root.returnToRail()
                onSignOutRequested: root.signOutRequested()
            }
        }
    }
}
