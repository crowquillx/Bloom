import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI
import "settings"

/**
 * SettingsScreen - Panel archetype (docs/style-guide.md, "Panels and rows").
 *
 * An ambient stage, the "Settings" PageHeader, then a category list on the
 * left (SettingsRail) and the selected category's rows on the right. Each
 * category is a standalone FocusScope inside a StackLayout. Settings update
 * immediately in memory; ConfigManager atomically persists ordinary changes
 * and coalesces high-frequency slider/session values.
 *
 * Keys: Right/Enter from the list enters the rows, Left/Escape from the rows
 * returns to the list, Up from the top of either is left unaccepted so Main
 * focuses the navigation bar, and Escape on the list goes back.
 */
FocusScope {
    id: root
    focus: true
    property string navigationId: "settings"

    // Build info from WindowManager context properties. Resolved here because inside
    // AboutAccountSettings these names would bind to its own same-named properties.
    readonly property string buildAppVersion: typeof appVersion !== "undefined" ? appVersion : ""
    readonly property string buildChannel: typeof appBuildChannel !== "undefined" ? appBuildChannel : ""
    readonly property string buildId: typeof appBuildId !== "undefined" ? appBuildId : ""
    readonly property string buildQtVersion: typeof qtVersion !== "undefined" ? qtVersion : ""
    property bool focusUpdatesOnActivate: false
    property bool focusSpacesOnActivate: false
    property bool saveToastArmed: false
    property string pendingSettingsToastMessage: qsTr("Settings saved")

    // The navigation bar recedes while the selected category is scrolled into
    // its rows; the page then slides up into the space the bar leaves.
    readonly property bool navigationRecessed: {
        if (activeSessionsVisible) return false
        var section = currentSectionItem()
        return !!section && section.scrolled === true
    }
    readonly property bool raisedLayout: navigationRecessed && root.activeFocus
    property Item _navigationReturnItem: null

    function saveFocusForNavigation() {
        var item = root.Window.activeFocusItem
        _navigationReturnItem = item && isWithinPage(item) ? item : null
    }

    function restoreFocusFromNavigation() {
        var item = _navigationReturnItem
        _navigationReturnItem = null
        if (activeSessionsVisible) {
            activeSessionsScreen.forceActiveFocus()
        } else if (item && item.visible && item.enabled && isWithinPage(item)) {
            item.forceActiveFocus()
        } else {
            settingsRail.focusRail()
        }
    }

    function isWithinPage(item) {
        for (var p = item; p; p = p.parent) {
            if (p === root) return true
        }
        return false
    }

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
        function onMpvProfilesChanged() {
            root.updateProfileNames()
            root.scheduleSettingsSavedToast()
        }
        function onDefaultProfileNameChanged() { root.scheduleSettingsSavedToast() }
        function onLibraryProfilesChanged() { root.scheduleSettingsSavedToast() }
        function onSeriesProfilesChanged() { root.scheduleSettingsSavedToast() }
        function onPlaybackCompletionThresholdChanged() { root.scheduleSettingsSavedToast() }
        function onSkipButtonAutoHideSecondsChanged() { root.scheduleSettingsSavedToast() }
        function onAudioDelayChanged() { root.scheduleSettingsSavedToast() }
        function onAudioPassthroughChanged() { root.scheduleSettingsSavedToast() }
        function onAudioExclusiveModeChanged() { root.scheduleSettingsSavedToast() }
        function onAudioOutputDeviceChanged() { root.scheduleSettingsSavedToast() }
        function onAutoplayNextEpisodeChanged() { root.scheduleSettingsSavedToast() }
        function onAutoplayCountdownSecondsChanged() { root.scheduleSettingsSavedToast() }
        function onStillWatchingEpisodeThresholdChanged() { root.scheduleSettingsSavedToast() }
        function onSleepTimerDefaultChanged() { root.scheduleSettingsSavedToast() }
        function onAutoSkipIntroChanged() { root.scheduleSettingsSavedToast() }
        function onAutoSkipOutroChanged() { root.scheduleSettingsSavedToast() }
        function onBackdropRotationIntervalChanged() { root.scheduleSettingsSavedToast() }
        function onLaunchInFullscreenChanged() { root.scheduleSettingsSavedToast() }
        function onThemeSongVolumeChanged() { root.scheduleSettingsSavedToast() }
        function onThemeSongLoopChanged() { root.scheduleSettingsSavedToast() }
        function onUiSoundsEnabledChanged() { root.scheduleSettingsSavedToast() }
        function onUiSoundsVolumeChanged() { root.scheduleSettingsSavedToast() }
        function onEnableFramerateMatchingChanged() { root.scheduleSettingsSavedToast() }
        function onFramerateMatchDelayChanged() { root.scheduleSettingsSavedToast() }
        function onEnableHDRChanged() { root.scheduleSettingsSavedToast() }
        function onLinuxRefreshRateCommandChanged() { root.scheduleSettingsSavedToast() }
        function onLinuxHDRCommandChanged() { root.scheduleSettingsSavedToast() }
        function onWindowsCustomHDRCommandChanged() { root.scheduleSettingsSavedToast() }
        function onThemeChanged() { root.scheduleSettingsSavedToast() }
        function onMdbListApiKeyChanged() { root.scheduleSettingsSavedToast() }
        function onSeerrBaseUrlChanged() { root.scheduleSettingsSavedToast() }
        function onSeerrApiKeyChanged() { root.scheduleSettingsSavedToast() }
        function onManualDpiScaleOverrideChanged() { root.scheduleSettingsSavedToast() }
        function onUpdateChannelChanged() { root.scheduleSettingsSavedToast() }
        function onAutoUpdateCheckEnabledChanged() { root.scheduleSettingsSavedToast() }
        function onLogLevelChanged() { root.scheduleSettingsSavedToast() }
        function onInputBindingsChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerEnabledChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerSourceChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerMaxItemsChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerAutoCycleEnabledChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerAutoCycleIntervalChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerBackdropSyncEnabledChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerHiddenItemTypesChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerLibraryUnwatchedOnlyChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerLibraryIdsChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerLogoPlacementChanged() { root.scheduleSettingsSavedToast() }
        function onHeroBannerInfoPlacementChanged() { root.scheduleSettingsSavedToast() }
        function onNextUpRewatchingChanged() { root.scheduleSettingsSavedToast() }
        function onNextUpCutoffDaysChanged() { root.scheduleSettingsSavedToast() }
        function onHomeFavoritesRowEnabledChanged() { root.scheduleSettingsSavedToast() }
        function onHomeRecommendationsEnabledChanged() { root.scheduleSettingsSavedToast() }
        function onScreensaverEnabledChanged() { root.scheduleSettingsSavedToast() }
        function onScreensaverModeChanged() { root.scheduleSettingsSavedToast() }
        function onScreensaverTimeoutSecondsChanged() { root.scheduleSettingsSavedToast() }
    }

    signal signOutRequested()
    property bool activeSessionsVisible: false

    function openActiveSessions() {
        activeSessionsVisible = true
        Qt.callLater(function() {
            if (activeSessionsVisible) activeSessionsScreen.forceActiveFocus()
        })
    }

    function closeActiveSessions() {
        activeSessionsVisible = false
        Qt.callLater(function() { aboutAccountSection.enterFromRail() })
    }
    function openNewProfileDialog(returnFocusTarget) {
        newProfileDialog.restoreFocusTarget = returnFocusTarget || null
        newProfileDialog.open()
    }

    function openDeleteProfileDialog(profileName, returnFocusTarget) {
        deleteProfileDialog.profileToDelete = profileName || ""
        deleteProfileDialog.restoreFocusTarget = returnFocusTarget || null
        deleteProfileDialog.open()
    }

    function openRenameProfileDialog(profileName, returnFocusTarget) {
        renameProfileDialog.profileToRename = profileName || ""
        renameProfileDialog.restoreFocusTarget = returnFocusTarget || null
        renameProfileDialog.open()
    }

    function openImportConfigDialog(returnFocusTarget) {
        importMpvConfigDialog.restoreFocusTarget = returnFocusTarget || null
        importMpvConfigDialog.open()
    }
    
    // ========================================
    // Focus Management
    // ========================================

    function findSectionIndex(sectionKey) {
        var sections = settingsRail.sectionModel || []
        for (var i = 0; i < sections.length; ++i) {
            if (sections[i].sectionKey === sectionKey) return i
        }
        return 0
    }

    // Open the Spaces section (picker "Manage spaces").
    function requestSpaceSectionFocus() {
        settingsRail.currentSection = findSectionIndex(settingsRail.spacesSectionKey)
        Qt.callLater(function() {
            spacesSection.enterFromRail()
        })
    }

    // Navigate to the About & Account section and focus updates controls.
    // Called by Main.qml when navigating to settings via "updates" sidebar item.
    function requestUpdateSectionFocus() {
        settingsRail.currentSection = findSectionIndex(settingsRail.aboutAccountSectionKey)
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

    function scheduleSettingsSavedToast(message) {
        if (!saveToastArmed || StackView.status !== StackView.Active) {
            return
        }
        pendingSettingsToastMessage = message || qsTr("Settings saved")
        settingsSavedToastTimer.restart()
    }

    function currentSectionItem() {
        switch (contentStack.currentIndex) {
        case 0: return playbackSection
        case 1: return homeSection
        case 2: return displaySection
        case 3: return videoSection
        case 4: return mpvSection
        case 5: return inputSection
        case 6: return integrationsSection
        case 7: return spacesSection
        case 8: return aboutAccountSection
        default: return null
        }
    }

    Component.onCompleted: {
        updateProfileNames()
        settingsRail.focusRail()
        Qt.callLater(function() {
            root.saveToastArmed = true
        })
    }

    Timer {
        id: settingsSavedToastTimer
        interval: 250
        repeat: false
        onTriggered: settingsSavedToast.show(root.pendingSettingsToastMessage)
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            if (focusUpdatesOnActivate) {
                requestUpdateSectionFocus()
                focusUpdatesOnActivate = false
            } else if (focusSpacesOnActivate) {
                requestSpaceSectionFocus()
                focusSpacesOnActivate = false
            } else {
                settingsRail.focusRail()
            }
        }
    }

    // ========================================
    // Stage
    // ========================================

    StageBackdrop {
        anchors.fill: parent
        mode: "ambient"
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
                "windowsRenderApi": "auto",
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

    RenameProfileDialog {
        id: renameProfileDialog
        onProfileRenamed: function(oldName, newName) {
            if (ConfigManager.renameMpvProfile(oldName, newName)) {
                mpvSection.selectProfile(newName)
            }
        }
    }

    ImportMpvConfigDialog {
        id: importMpvConfigDialog
        onProfileImported: function(name) {
            ConfigManager.defaultProfileName = name
            mpvSection.selectProfile(name)
        }
    }

    // ========================================
    // Layout: header, category list + rows
    // ========================================

    Item {
        id: pageLayout
        enabled: !root.activeSessionsVisible
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pageMarginX
        anchors.rightMargin: Theme.pageMarginX
        // Slides up into the bar's space while the bar is receded.
        y: root.raisedLayout ? Theme.spacingXLarge : Theme.pageMarginTop
        height: parent.height - y

        Behavior on y {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: Theme.durationLong; easing.type: Theme.easingEmphasized }
        }

        PageHeader {
            id: pageHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            title: qsTr("Settings")
        }

        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: pageHeader.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingXLarge
            spacing: Theme.pageMarginX

            // Category list
            SettingsRail {
                id: settingsRail
                Layout.fillHeight: true
                Layout.preferredWidth: implicitWidth
                Layout.bottomMargin: Theme.pageMarginBottom

                onEnterContentRequested: root.enterContentPanel()
            }

            // Rows of the selected category. Capped width so rows keep their
            // proportions on ultrawide screens.
            StackLayout {
                id: contentStack
                Layout.fillWidth: true
                Layout.maximumWidth: Math.round(1460 * Theme.layoutScale)
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                currentIndex: settingsRail.currentSection

                PlaybackSettings {
                    id: playbackSection
                    onRequestReturnToRail: root.returnToRail()
                }

                HomeSettings {
                    id: homeSection
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
                    onOpenImportConfigDialog: function(returnTarget) { root.openImportConfigDialog(returnTarget) }
                    onOpenDeleteProfileDialog: function(name, returnTarget) { root.openDeleteProfileDialog(name, returnTarget) }
                    onOpenRenameProfileDialog: function(name, returnTarget) { root.openRenameProfileDialog(name, returnTarget) }
                }

                InputSettings {
                    id: inputSection
                    onRequestReturnToRail: root.returnToRail()
                }

                IntegrationsSettings {
                    id: integrationsSection
                    onRequestReturnToRail: root.returnToRail()
                }

                SpaceSettings {
                    id: spacesSection
                    onRequestReturnToRail: root.returnToRail()
                }

                AboutAccountSettings {
                    id: aboutAccountSection
                    appVersion: root.buildAppVersion
                    appBuildChannel: root.buildChannel
                    appBuildId: root.buildId
                    qtVersion: root.buildQtVersion
                    onRequestReturnToRail: root.returnToRail()
                    onSignOutRequested: root.signOutRequested()
                    onSessionsRequested: root.openActiveSessions()
                }
            }
        }
    }

    ActiveSessionsScreen {
        id: activeSessionsScreen
        anchors.fill: parent
        z: 20
        visible: root.activeSessionsVisible
        enabled: visible
        focus: visible
        sessionService: typeof SessionService !== "undefined" ? SessionService : null
        authService: typeof AuthenticationService !== "undefined" ? AuthenticationService : null
        onBackRequested: root.closeActiveSessions()
    }

    ToastNotification {
        id: settingsSavedToast
        z: 10
    }
}
