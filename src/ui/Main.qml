import QtQuick
import QtQuick.Window 2.15
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

Window {
    id: window
    width: 1280
    height: 720
    visible: true
    title: "Bloom"
    color: Theme.backgroundPrimary
    
    Component.onCompleted: {
        console.log("=== Main.qml: Window Component.onCompleted ===")
        console.log("Main.qml: AuthenticationService =", AuthenticationService)
        console.log("Main.qml: AuthenticationService.authenticated =", AuthenticationService.authenticated)

        // Apply fullscreen setting on startup
        if (ConfigManager.launchInFullscreen) {
            console.log("Main.qml: Launching in fullscreen mode")
            showFullScreen()
        }

        startupUpdateTimer.start()
    }
    
    // ========================================
    // Font Loader for Material Symbols
    // ========================================
    FontLoader {
        id: materialSymbolsFont
        source: "qrc:/fonts/MaterialSymbolsOutlined.ttf"
        onStatusChanged: {
            if (status === FontLoader.Ready) {
                console.log("FontLoader: Material Symbols loaded, name:", name)
            } else if (status === FontLoader.Error) {
                console.log("FontLoader: Failed to load Material Symbols font")
            }
        }
    }
    
    // ========================================
    // Properties
    // ========================================
    
    /// Whether the user is logged in (controls sidebar visibility)
    property bool isLoggedIn: false
    property var sidebarProxy: sidebarLoader.item || sidebarStub
    
    /// Sidebar overlay mode threshold (narrow screens use overlay)
    readonly property int overlayThreshold: 960
    
    /// Current content offset based on sidebar state
    readonly property int contentOffset: {
        if (!isLoggedIn) return 0
        if (sidebarProxy.overlayMode) return 0
        return sidebarProxy.sidebarWidth
    }
    readonly property bool embeddedPlaybackActive: PlayerController.supportsEmbeddedVideo && PlayerController.isPlaybackActive
    readonly property bool useDetachedPlaybackOverlayWindow: Qt.platform.os === "windows"
    readonly property bool playbackOverlayNavigationActive: embeddedPlaybackActive
                                                         && activeEmbeddedPlaybackOverlay.fullControlsVisible
    readonly property bool playbackSelectorOpen: embeddedPlaybackActive
                                              && activeEmbeddedPlaybackOverlay.selectorOpen
    readonly property bool awaitingUpNextTransition: PlayerController.awaitingNextEpisodeResolution
                                                  && !PlayerController.isPlaybackActive
    property bool pendingStartupUpdatePopup: false

    function ensureMediaSourceSelectionDialog() {
        if (!mediaSourceSelectionDialogLoader.active) {
            mediaSourceSelectionDialogLoader.active = true
        }
        return mediaSourceSelectionDialogLoader.item
    }

    function openStartupUpdateDialog() {
        if (!updateDialogLoader.active) {
            updateDialogLoader.active = true
        }

        const dialog = updateDialogLoader.item
        if (!dialog) {
            Qt.callLater(openStartupUpdateDialog)
            return
        }

        if (dialog.visible) {
            return
        }

        dialog.open()
        Qt.callLater(function() {
            if (dialog.primaryButton) {
                dialog.primaryButton.forceActiveFocus()
            }
        })
    }

    function ensurePlaybackOverlayFocus() {
        if (!embeddedPlaybackActive) {
            return
        }
        activeEmbeddedPlaybackOverlay.showControls()
        window.requestActivate()
        if (useDetachedPlaybackOverlayWindow) {
            embeddedOverlayWindow.requestActivate()
        }
        Qt.callLater(function() {
            activeEmbeddedPlaybackOverlay.activateOverlayFocus()
            Qt.callLater(function() {
                activeEmbeddedPlaybackOverlay.activateOverlayFocus()
            })
        })
    }

    VideoSurface {
        id: videoSurface
        anchors.fill: parent
        visible: embeddedPlaybackActive
        z: 900
    }

    EmbeddedPlaybackOverlay {
        id: embeddedPlaybackOverlayInline
        anchors.fill: parent
        visible: embeddedPlaybackActive && !useDetachedPlaybackOverlayWindow
        z: 950
    }

    property var activeEmbeddedPlaybackOverlay: useDetachedPlaybackOverlayWindow
                                                ? embeddedPlaybackOverlayDetached
                                                : embeddedPlaybackOverlayInline

    Window {
        id: embeddedOverlayWindow
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        transientParent: window
        modality: Qt.NonModal
        color: "transparent"
        visible: useDetachedPlaybackOverlayWindow
                 && window.visible
                 && window.visibility !== Window.Minimized
                 && embeddedPlaybackActive
                 && PlayerController.playbackState !== PlayerController.Loading
        x: window.x
        y: window.y
        width: window.width
        height: window.height
        onVisibleChanged: {
            // Do not auto-open playback controls when overlay window becomes visible.
        }

        EmbeddedPlaybackOverlay {
            id: embeddedPlaybackOverlayDetached
            anchors.fill: parent
        }
    }

    Connections {
        target: PlayerController
        function onIsPlaybackActiveChanged() {
            // Do not auto-open controls on playback start.
        }
        function onPlaybackStateChanged() {
            if (!embeddedPlaybackActive) {
                return
            }
            if (PlayerController.playbackState === PlayerController.Paused
                    && PlayerController.currentPositionSeconds > 0.5) {
                ensurePlaybackOverlayFocus()
            }
        }
    }
    
    // ========================================
    // Main Content Area
    // ========================================
    
    // Focus scope for main content to enable keyboard navigation to/from sidebar
    FocusScope {
        id: mainContentArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: contentOffset
        visible: !embeddedPlaybackActive
        property bool restoringFocusFromSidebar: false
        
        Behavior on anchors.leftMargin {
            NumberAnimation { 
                duration: SidebarSettings.reduceMotion ? 0 : Theme.durationNormal 
                easing.type: Easing.OutCubic
            }
        }

        function saveFocusForSidebar() {
            var currentItem = stackView.currentItem
            if (currentItem && typeof currentItem["saveFocusForSidebar"] === "function") {
                currentItem["saveFocusForSidebar"]()
            }
        }

        function restoreFocusFromSidebar() {
            restoringFocusFromSidebar = true
            var currentItem = stackView.currentItem
            if (currentItem && typeof currentItem["restoreFocusFromSidebar"] === "function") {
                currentItem["restoreFocusFromSidebar"]()
            } else if (currentItem) {
                currentItem.forceActiveFocus()
            } else {
                forceActiveFocus()
            }
            Qt.callLater(function() {
                restoringFocusFromSidebar = false
            })
        }
        
        // Handle Left arrow to focus sidebar when at left edge
        Keys.onLeftPressed: function(event) {
            if (isLoggedIn) {
                saveFocusForSidebar()
                if (sidebarProxy.expanded) {
                    // When sidebar is expanded, focus first nav item
                    sidebarProxy.focusNavigation()
                    event.accepted = true
                } else if (!sidebarProxy.overlayMode) {
                    // When sidebar is collapsed (rail mode), focus hamburger
                    sidebarProxy.focusHamburger()
                    event.accepted = true
                } else {
                    event.accepted = false
                }
            } else {
                event.accepted = false
            }
        }

        // Explicitly forward focus to content when this scope receives it
        onActiveFocusChanged: {
            if (restoringFocusFromSidebar) {
                return
            }
            var currentItem = stackView.currentItem
            if (currentItem
                    && currentItem["restoringFocusFromSeriesDetailsReturn"]) {
                return
            }
            if (activeFocus && currentItem && !currentItem.activeFocus) {
                console.log("[FocusDebug] mainContentArea got focus, forwarding to currentItem")
                const item = currentItem
                Qt.callLater(function() {
                    if (!mainContentArea.activeFocus || !item || item !== stackView.currentItem || item.activeFocus) {
                        return
                    }
                    item.forceActiveFocus()
                })
            }
        }
        
        StackView {
            id: stackView
            anchors.fill: parent
            initialItem: "LoginScreen.qml"
            
            onCurrentItemChanged: {
                console.log("[FocusDebug] StackView.currentItemChanged:", currentItem ? currentItem.toString() : "null", "sidebar.expanded:", sidebarProxy.expanded)
                // Let screens with restoreFocusState handle their own focus restoration
                // This allows HomeScreen to restore focus to the previously selected item
                if (currentItem && !sidebarProxy.expanded) {
                    if (typeof currentItem["restoreFocusState"] === "function") {
                        console.log("[FocusDebug] Screen has restoreFocusState, letting it handle focus")
                        // Don't force focus here - let StackView.onStatusChanged in the screen handle it
                    } else {
                        console.log("[FocusDebug] Setting focus to currentItem (no restoreFocusState)")
                        currentItem.forceActiveFocus()
                    }
                } else if (currentItem && sidebarProxy.expanded) {
                    console.log("[FocusDebug] Skipping focus - sidebar is expanded, will restore later")
                }
                updateSidebarNavigation()
            }
        }
    }

    Rectangle {
        id: upNextBlockingOverlay
        anchors.fill: parent
        z: 850
        visible: awaitingUpNextTransition
        color: Qt.rgba(0, 0, 0, 0.88)

        MouseArea {
            anchors.fill: parent
            enabled: awaitingUpNextTransition
            hoverEnabled: enabled
            acceptedButtons: Qt.AllButtons
            preventStealing: true
            onPressed: function(mouse) { mouse.accepted = true }
            onReleased: function(mouse) { mouse.accepted = true }
            onClicked: function(mouse) { mouse.accepted = true }
            onWheel: function(wheel) { wheel.accepted = true }
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Theme.spacingMedium

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Up Next")
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.bold: true
                color: Theme.textPrimary
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: upNextBlockingOverlay.visible
                Layout.preferredWidth: Math.round(52 * Theme.layoutScale)
                Layout.preferredHeight: Math.round(52 * Theme.layoutScale)
                palette.dark: Theme.textPrimary
                palette.light: Theme.accentSecondary
            }
        }
    }

    Connections {
        target: PlayerController
        function onPlaybackVersionSelectionRequested(requestId, dialogModel, restoreFocusHint) {
            const dialog = ensureMediaSourceSelectionDialog()
            if (dialog) {
                dialog.openForRequest(requestId, dialogModel, restoreFocusHint)
            } else {
                Qt.callLater(function() {
                    const deferredDialog = ensureMediaSourceSelectionDialog()
                    if (deferredDialog) {
                        deferredDialog.openForRequest(requestId, dialogModel, restoreFocusHint)
                    }
                })
            }
        }
    }

    Loader {
        id: mediaSourceSelectionDialogLoader
        active: false
        sourceComponent: MediaSourceSelectionDialog {
        }
    }

    Timer {
        id: startupUpdateTimer
        interval: 1500
        repeat: false
        onTriggered: UpdateService.performStartupCheck()
    }

    Loader {
        id: updateDialogLoader
        active: false

        sourceComponent: Dialog {
            id: updateDialog
            property alias primaryButton: updatePrimaryButton
            parent: Overlay.overlay
            modal: true
            focus: true
            anchors.centerIn: parent
            width: Math.round(720 * Theme.layoutScale)
            padding: Theme.spacingLarge

            onRejected: UpdateService.dismissStartupPopup()

            background: Rectangle {
                color: Theme.cardBackground
                radius: Theme.radiusMedium
                border.color: Theme.cardBorder
                border.width: 1
            }

            header: Rectangle {
                color: "transparent"
                height: Math.round(84 * Theme.layoutScale)

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLarge
                    spacing: Theme.spacingSmall

                    Text {
                        text: qsTr("Update Available")
                        font.pixelSize: Theme.fontSizeTitle
                        font.family: Theme.fontPrimary
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }

                    Text {
                        text: UpdateService.availableVersion.length > 0
                              ? qsTr("Bloom %1 is available on the %2 channel.")
                                    .arg(UpdateService.availableVersion)
                                    .arg(UpdateService.availableChannel)
                              : qsTr("A Bloom update is available.")
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }
                }
            }

            contentItem: ColumnLayout {
                spacing: Theme.spacingMedium

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(updateDialogNotesText.implicitHeight + Theme.spacingMedium,
                                                     Math.round(window.height * 0.32))
                    Layout.maximumHeight: Math.round(window.height * 0.32)
                    clip: true

                    Text {
                        id: updateDialogNotesText
                        width: parent.width
                        text: UpdateService.releaseNotes.length > 0
                              ? UpdateService.releaseNotes
                              : qsTr("Open Settings > Updates for full details and download options.")
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap
                    }
                }

                Text {
                    text: UpdateService.applySupported
                          ? qsTr("Bloom can download and launch the installer for you.")
                          : qsTr("This build cannot auto-install updates, but download links are available.")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            footer: Item {
                implicitHeight: footerLayout.implicitHeight + (Theme.spacingLarge * 2)

                RowLayout {
                    id: footerLayout
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLarge
                    spacing: Theme.spacingMedium

                    Button {
                        id: updatePrimaryButton
                        text: UpdateService.applySupported ? qsTr("Download and Install") : qsTr("Open Download Page")
                        enabled: !UpdateService.downloadInProgress && !UpdateService.installerLaunched
                        onClicked: {
                            if (UpdateService.applySupported) {
                                updateDialog.close()
                                UpdateService.downloadAndInstallUpdate()
                            } else {
                                UpdateService.openUpdateDownloadPage()
                                updateDialog.close()
                            }
                        }
                    }

                    Button {
                        text: qsTr("Later")
                        onClicked: {
                            UpdateService.dismissStartupPopup()
                            updateDialog.close()
                        }
                    }
                }
            }
        }
    }
    
    // ========================================
    // Sidebar Navigation Shell
    // ========================================
    
    QtObject {
        id: sidebarStub
        property bool expanded: false
        property bool overlayMode: false
        property int sidebarWidth: 0
        property string currentNavigation: "home"
        property string currentLibraryId: ""
        function focusNavigation() {}
        function focusHamburger() {}
        function close() {}
        function toggle() {}
    }

    Loader {
        id: sidebarLoader
        anchors.fill: parent
        active: isLoggedIn
        visible: active

        sourceComponent: Sidebar {
            visible: !embeddedPlaybackActive
            overlayMode: window.width < overlayThreshold
            currentNavigation: "home"
            mainContent: mainContentArea  // Connect to main content for focus navigation

            onNavigationRequested: function(navigationId) {
                playPointerSelectSound()
                // Handle navigation requests
                switch (navigationId) {
                    case "home":
                        // Save focus state before navigating
                        var homeScreen = stackView.find(function(item) { return item && item["navigationId"] === "home" })
                        if (homeScreen && typeof homeScreen["saveFocusState"] === "function") homeScreen["saveFocusState"]()
                        // Pop back to home screen
                        while (stackView.depth > 1) {
                            stackView.pop()
                        }
                        break
                    case "search":
                        // Save focus state before navigating
                        var homeForSearch = stackView.find(function(item) { return item && item["navigationId"] === "home" })
                        if (homeForSearch && typeof homeForSearch["saveFocusState"] === "function") homeForSearch["saveFocusState"]()
                        // Navigate to search screen
                        pushSearchScreen()
                        break
                    case "settings":
                        // Save focus state before navigating
                        var homeForSettings = stackView.find(function(item) { return item && item["navigationId"] === "home" })
                        if (homeForSettings && typeof homeForSettings["saveFocusState"] === "function") homeForSettings["saveFocusState"]()
                        // Navigate to settings screen
                        pushSettingsScreen()
                        break
                    case "updates":
                        // Save focus state before navigating
                        var homeForUpdates = stackView.find(function(item) { return item && item["navigationId"] === "home" })
                        if (homeForUpdates && typeof homeForUpdates["saveFocusState"] === "function") homeForUpdates["saveFocusState"]()
                        pushSettingsScreen({ focusUpdatesOnActivate: true })
                        break
                }
            }

            onLibraryRequested: function(libraryId, libraryName) {
                playPointerSelectSound()
                if (!libraryId)
                    return
                // Save focus state before navigating
                var homeScreenForLibrary = stackView.find(function(item) { return item && item["navigationId"] === "home" })
                if (homeScreenForLibrary && typeof homeScreenForLibrary["saveFocusState"] === "function") homeScreenForLibrary["saveFocusState"]()
                stackView.push("LibraryScreen.qml", {
                    currentParentId: libraryId,
                    currentLibraryId: libraryId,
                    currentLibraryName: libraryName
                })
            }

            onSignOutRequested: {
                // Trigger logout process
                AuthenticationService.logout()
            }

            onExitRequested: {
                // Exit the application (saves config and quits)
                ConfigManager.exitApplication()
            }
        }
    }
    
    // Function to push settings screen and wire up signals
    function updateSidebarNavigation() {
        var item = stackView.currentItem
        var navigationId = (item && item["navigationId"]) ? item["navigationId"] : "home"
        sidebarProxy.currentNavigation = navigationId
        if (navigationId && navigationId.indexOf("library:") === 0) {
            sidebarProxy.currentLibraryId = navigationId.substring("library:".length)
        } else {
            sidebarProxy.currentLibraryId = ""
        }
    }

    function playPointerSelectSound() {
        if (InputModeManager.pointerActive) {
            UiSoundController.playSelect()
        }
    }

    function pushSettingsScreen(options) {
        options = options || {}
        playPointerSelectSound()
        stackView.push("SettingsScreen.qml", {
            focusUpdatesOnActivate: !!options.focusUpdatesOnActivate
        })
        Qt.callLater(function() {
            var settingsScreen = stackView.currentItem
            if (settingsScreen && settingsScreen["signOutRequested"]) {
                settingsScreen["signOutRequested"].connect(function() {
                    AuthenticationService.logout()
                })
            }
            if (settingsScreen && options.focusUpdatesOnActivate && typeof settingsScreen["requestUpdateSectionFocus"] === "function") {
                settingsScreen["requestUpdateSectionFocus"]()
            }
        })
        updateSidebarNavigation()
    }
    
    // Function to push search screen and wire up signals
    function pushSearchScreen() {
        playPointerSelectSound()
        var searchScreen = stackView.push("SearchScreen.qml")
        if (searchScreen) {
            // Connect navigateToMovie signal - show movie details view
            searchScreen.navigateToMovie.connect(function(movieData) {
                playPointerSelectSound()
                // Get library info from the movie data if available
                var libraryId = movieData.ParentId || ""
                var libraryName = movieData.LibraryName || ""
                stackView.push("LibraryScreen.qml", {
                    currentParentId: libraryId,
                    currentLibraryId: libraryId,
                    currentLibraryName: libraryName,
                    currentMovieData: movieData,
                    showMovieDetails: true,
                    preferStackPopOnDirectBack: true
                })
            })
            
            // Connect navigateToSeries signal - show series details view
            searchScreen.navigateToSeries.connect(function(seriesId) {
                playPointerSelectSound()
                stackView.push("LibraryScreen.qml", {
                    currentParentId: "",
                    currentLibraryId: "",
                    currentLibraryName: "",
                    currentSeriesId: seriesId,
                    showSeriesDetails: true,
                    preferStackPopOnDirectBack: true
                })
                // Load series details, seasons, and next episode
                LibraryService.getSeriesDetails(seriesId)
                LibraryService.getItems(seriesId, 0, 0)  // Load seasons
                LibraryService.getNextUnplayedEpisode(seriesId)
            })
        }
        updateSidebarNavigation()
    }
    
    // ========================================
    // Toast Notification
    // ========================================
    
    ToastNotification {
        id: toast
        z: 1000  // Above all other content
    }
    
    // Global input sounds
    Connections {
        target: InputModeManager
        function onNavigationKeyPressed() { UiSoundController.playNavigation() }
        function onSelectKeyPressed() { UiSoundController.playSelect() }
        function onBackKeyPressed() { UiSoundController.playBack() }
        ignoreUnknownSignals: true
    }

    Connections {
        target: UpdateService
        ignoreUnknownSignals: true
        function onStartupPopupRequested() {
            if (!window.isLoggedIn && !AuthenticationService.authenticated) {
                pendingStartupUpdatePopup = true
                return
            }
            pendingStartupUpdatePopup = false
            openStartupUpdateDialog()
        }
    }
    
    // ========================================
    // AuthenticationService Connections
    // ========================================

    // Global connection to handle login success navigation
    Connections {
        target: AuthenticationService
        
        function onSessionExpiredAfterPlayback() {
            console.log("Main.qml: Session expired after playback, showing toast")
            // Show toast explaining what happened
            toast.show(qsTr("Your session has expired. Please log in again."))
        }
        
        function onLoginSuccess(userId, accessToken, username) {
            console.log("=== Main.qml: onLoginSuccess CALLED ===")
            console.log("Main.qml: userId=", userId, "username=", username)
            console.log("Main.qml: stackView.depth before replace:", stackView.depth)
            console.log("Main.qml: stackView.currentItem before replace:", stackView.currentItem)
            
            // Mark as logged in to show sidebar
            window.isLoggedIn = true
            console.log("Main.qml: isLoggedIn set to true")
            
            // Replace instead of push to prevent going back to login
            console.log("Main.qml: About to call stackView.replace with HomeScreen.qml")
            var homeScreen = stackView.replace("HomeScreen.qml")
            console.log("Main.qml: stackView.replace returned:", homeScreen)
            console.log("Main.qml: stackView.depth after replace:", stackView.depth)
            console.log("Main.qml: stackView.currentItem after replace:", stackView.currentItem)
            
            if (homeScreen) {
                console.log("Main.qml: homeScreen is valid, connecting signals...")
                // Connect navigateToLibrary signal
                homeScreen.navigateToLibrary.connect(function(libraryId, libraryName) {
                    playPointerSelectSound()
                    stackView.push("LibraryScreen.qml", {
                        currentParentId: libraryId,
                        currentLibraryId: libraryId,
                        currentLibraryName: libraryName
                    })
                })
                
                // Connect navigateToMovie signal - show movie details view
                homeScreen.navigateToMovie.connect(function(movieData, libraryId, libraryName) {
                    playPointerSelectSound()
                    stackView.push("LibraryScreen.qml", {
                        currentParentId: libraryId,
                        currentLibraryId: libraryId,
                        currentLibraryName: libraryName,
                        directNavigationMode: true,
                        returnToHomeOnDirectBack: true
                    })
                    // Defer calling showMovieDetailsView until screen is ready
                    Qt.callLater(function() {
                        var screen = stackView.currentItem
                        if (screen && typeof screen["showMovieDetailsView"] === "function") {
                            screen["showMovieDetailsView"](movieData)
                        }
                    })
                })
                
                // Connect navigateToEpisode signal - show episode view
                homeScreen.navigateToEpisode.connect(function(episodeData, seriesId, libraryId, libraryName) {
                    playPointerSelectSound()
                    stackView.push("LibraryScreen.qml", {
                        currentParentId: libraryId,
                        currentLibraryId: libraryId,
                        currentLibraryName: libraryName,
                        currentSeriesId: seriesId,
                        directNavigationMode: true,
                        returnToHomeOnDirectBack: true
                    })
                    // Need to load series details first for context, then show episode
                    LibraryService.getSeriesDetails(seriesId)
                    // Defer calling showEpisodeDetails until screen is ready
                    Qt.callLater(function() {
                        var screen = stackView.currentItem
                        if (screen && typeof screen["showEpisodeDetails"] === "function") {
                            screen["showEpisodeDetails"](episodeData)
                        }
                    })
                })
                
                // Connect navigateToSeason signal - show season view
                homeScreen.navigateToSeason.connect(function(seasonId, seasonNumber, seriesId, libraryId, libraryName) {
                    playPointerSelectSound()
                    stackView.push("LibraryScreen.qml", {
                        currentParentId: libraryId,
                        currentLibraryId: libraryId,
                        currentLibraryName: libraryName,
                        currentSeriesId: seriesId,
                        currentSeasonId: seasonId,
                        currentSeasonNumber: seasonNumber,
                        showSeasonView: true,
                        directNavigationMode: true,
                        returnToHomeOnDirectBack: true
                    })
                    // Load series details for logo/poster context
                    LibraryService.getSeriesDetails(seriesId)
                    // Load season episodes
                    SeriesDetailsViewModel.loadSeasonEpisodes(seasonId)
                })
                
                // Connect navigateToSeries signal - show series details view
                homeScreen.navigateToSeries.connect(function(seriesId, libraryId, libraryName) {
                    playPointerSelectSound()
                    stackView.push("LibraryScreen.qml", {
                        currentParentId: libraryId,
                        currentLibraryId: libraryId,
                        currentLibraryName: libraryName,
                        currentSeriesId: seriesId,
                        showSeriesDetails: true,
                        directNavigationMode: true,
                        returnToHomeOnDirectBack: true
                    })
                    // Load series details, seasons, and next episode
                    LibraryService.getSeriesDetails(seriesId)
                    LibraryService.getItems(seriesId, 0, 0)  // Load seasons
                    LibraryService.getNextUnplayedEpisode(seriesId)
                })
            }
            updateSidebarNavigation()

            if (pendingStartupUpdatePopup) {
                Qt.callLater(function() {
                    if (window.isLoggedIn) {
                        pendingStartupUpdatePopup = false
                        openStartupUpdateDialog()
                    }
                })
            }
        }
        
        function onLoggedOut() {
            console.log("Main.qml: Logged out, returning to login screen")
            
            // Mark as logged out to hide sidebar
            window.isLoggedIn = false
            
            // Close sidebar if open
            sidebarProxy.close()
            
            // Clear all screens and go back to login
            stackView.clear()
            stackView.push("LoginScreen.qml")
        }
        
        ignoreUnknownSignals: true
    }
    
    // Connection to handle post-playback navigation to next episode
    Connections {
        target: PlayerController
        
        function onNavigateToNextEpisode(episodeData, seriesId, lastAudioIndex, lastSubtitleIndex, autoplay) {
            console.log("[Main] Up Next screen for:", 
                        episodeData.SeriesName, "S" + episodeData.ParentIndexNumber + "E" + episodeData.IndexNumber,
                        "Autoplay:", autoplay)
            
            // Pop back to the root level (home screen) first
            while (stackView.depth > 1) {
                stackView.pop(null, StackView.Immediate)
            }
            
            // Push the Up Next interstitial screen
            var upNextScreen = stackView.push("UpNextScreen.qml", {
                episodeData: episodeData,
                seriesId: seriesId,
                lastAudioIndex: lastAudioIndex,
                lastSubtitleIndex: lastSubtitleIndex,
                autoplay: autoplay
            }, StackView.Immediate)
            
            if (upNextScreen) {
                Qt.callLater(function() {
                    if (upNextScreen && upNextScreen.focusPrimaryAction) {
                        upNextScreen.focusPrimaryAction()
                    }
                })
                // Play the next episode
                upNextScreen.playRequested.connect(function() {
                    console.log("[Main] Up Next: Play requested")
                    stackView.pop(null, StackView.Immediate)
                    PlayerController.playNextEpisode(episodeData, seriesId)
                })
                
                // Navigate to the episode list (same as ESC)
                upNextScreen.moreEpisodesRequested.connect(function() {
                    console.log("[Main] Up Next: More episodes requested")
                    stackView.pop(null, StackView.Immediate)
                    PlayerController.clearPendingAutoplayContext()

                    var targetEpisodeData = Object.assign({}, episodeData || {})
                    if (!targetEpisodeData.itemId && targetEpisodeData.Id) {
                        targetEpisodeData.itemId = targetEpisodeData.Id
                    }
                    if (!targetEpisodeData.SeasonId && targetEpisodeData.ParentId) {
                        targetEpisodeData.SeasonId = targetEpisodeData.ParentId
                    }
                    
                    var destinationScreen = stackView.currentItem
                    var targetSeasonId = targetEpisodeData.SeasonId || ""

                    if (destinationScreen
                            && destinationScreen.handlesOwnBackNavigation === true
                            && typeof destinationScreen.showEpisodeDetails === "function") {
                        console.log("[Main] Up Next: Reusing existing LibraryScreen for episode navigation")
                        destinationScreen.pendingAudioTrackIndex = lastAudioIndex
                        destinationScreen.pendingSubtitleTrackIndex = lastSubtitleIndex
                        if (seriesId) {
                            destinationScreen.currentSeriesId = seriesId
                        }
                        if (targetSeasonId) {
                            destinationScreen.currentSeasonId = targetSeasonId
                        }
                        Qt.callLater(function() {
                            if (destinationScreen && typeof destinationScreen.showEpisodeDetails === "function") {
                                destinationScreen.showEpisodeDetails(targetEpisodeData, true)
                            }
                        })
                        return
                    }

                    var libraryScreen = stackView.push("LibraryScreen.qml", {
                        currentParentId: "",
                        currentLibraryId: "",
                        currentLibraryName: "",
                        currentSeriesId: seriesId,
                        currentSeasonId: targetSeasonId,
                        showSeasonView: true,
                        directNavigationMode: true,
                        preferStackPopOnDirectBack: true,
                        pendingAudioTrackIndex: lastAudioIndex,
                        pendingSubtitleTrackIndex: lastSubtitleIndex
                    })

                    LibraryService.getSeriesDetails(seriesId)
                    if (libraryScreen) {
                        libraryScreen.pendingEpisodeData = targetEpisodeData
                    } else {
                        console.warn("[Main] Up Next: LibraryScreen push failed, could not set pendingEpisodeData")
                    }
                })
                
                // Go back to home
                upNextScreen.backToHomeRequested.connect(function() {
                    console.log("[Main] Up Next: Back to home requested")
                    PlayerController.clearPendingAutoplayContext()
                    stackView.pop(null, StackView.Immediate)
                })
            } else {
                console.warn("[Main] Failed to create Up Next screen, clearing pending autoplay context")
                PlayerController.clearPendingAutoplayContext()
            }
        }
        
        ignoreUnknownSignals: true
    }

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: Qt.quit()
    }
    Shortcut {
        sequences: ["Esc"]
        enabled: !PlayerController.isPlaybackActive
                 && stackView.depth > 1
                 && !sidebarProxy.expanded
                 && !(stackView.currentItem && stackView.currentItem["handlesOwnBackNavigation"] === true)
        onActivated: {
            console.log("[FocusDebug] Back shortcut activated, stackView.depth:", stackView.depth, "sidebar.expanded:", sidebarProxy.expanded)
            if (stackView.depth > 1) {
                if (InputModeManager.pointerActive) UiSoundController.playBack()
                stackView.pop()
                Qt.callLater(updateSidebarNavigation)
            }
        }
    }
    
    // Sidebar toggle shortcut
    Shortcut {
        sequence: "M"
        enabled: isLoggedIn
        onActivated: sidebarProxy.toggle()
    }

    Shortcut {
        sequence: "F11"
        onActivated: {
            if (window.visibility === Window.FullScreen)
                window.showNormal()
            else
                window.showFullScreen()
        }
    }
    
    Shortcut {
        sequence: "Alt+Enter"
        onActivated: {
            if (window.visibility === Window.FullScreen)
                window.showNormal()
            else
                window.showFullScreen()
        }
    }

    Shortcut {
        sequences: ["Space", "K"]
        enabled: PlayerController.isPlaybackActive
        onActivated: {
            activeEmbeddedPlaybackOverlay.showControls()
            PlayerController.togglePause()
        }
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: PlayerController.isPlaybackActive && activeEmbeddedPlaybackOverlay.skipPopupVisible
        onActivated: {
            activeEmbeddedPlaybackOverlay.triggerActiveSkip()
        }
    }

    Shortcut {
        sequence: "Esc"
        enabled: PlayerController.isPlaybackActive
        onActivated: {
            if (activeEmbeddedPlaybackOverlay.closeSelectors()) {
                return
            }
            PlayerController.stop()
        }
    }

    Shortcut {
        sequence: "Left"
        enabled: PlayerController.isPlaybackActive && !playbackSelectorOpen
        onActivated: {
            if (playbackOverlayNavigationActive) {
                activeEmbeddedPlaybackOverlay.handleDirectionalKey("left")
            } else {
                activeEmbeddedPlaybackOverlay.showSeekPreview()
                PlayerController.seekRelative(-10)
            }
        }
    }

    Shortcut {
        sequence: "Right"
        enabled: PlayerController.isPlaybackActive && !playbackSelectorOpen
        onActivated: {
            if (playbackOverlayNavigationActive) {
                activeEmbeddedPlaybackOverlay.handleDirectionalKey("right")
            } else {
                activeEmbeddedPlaybackOverlay.showSeekPreview()
                PlayerController.seekRelative(10)
            }
        }
    }

    Shortcut {
        sequence: "Up"
        enabled: embeddedPlaybackActive && !playbackSelectorOpen
        onActivated: activeEmbeddedPlaybackOverlay.handleDirectionalKey("up")
    }

    Shortcut {
        sequence: "Down"
        enabled: embeddedPlaybackActive && !playbackSelectorOpen
        onActivated: activeEmbeddedPlaybackOverlay.handleDirectionalKey("down")
    }

    Shortcut {
        sequence: "J"
        enabled: PlayerController.isPlaybackActive
        onActivated: {
            activeEmbeddedPlaybackOverlay.showSeekPreview()
            PlayerController.seekRelative(-10)
        }
    }

    Shortcut {
        sequence: "L"
        enabled: PlayerController.isPlaybackActive
        onActivated: {
            activeEmbeddedPlaybackOverlay.showSeekPreview()
            PlayerController.seekRelative(10)
        }
    }

    Shortcut {
        sequence: "S"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.stop()
    }

    Shortcut {
        sequence: "I"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.toggleMpvStats()
    }

    Shortcut {
        sequence: "Shift+I"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsOnce()
    }

    Shortcut {
        sequence: "0"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(0)
    }

    Shortcut {
        sequence: "1"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(1)
    }

    Shortcut {
        sequence: "2"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(2)
    }

    Shortcut {
        sequence: "3"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(3)
    }

    Shortcut {
        sequence: "4"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(4)
    }

    Shortcut {
        sequence: "5"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(5)
    }

    Shortcut {
        sequence: "6"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(6)
    }

    Shortcut {
        sequence: "7"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(7)
    }

    Shortcut {
        sequence: "8"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(8)
    }

    Shortcut {
        sequence: "9"
        enabled: PlayerController.isPlaybackActive
        onActivated: PlayerController.showMpvStatsPage(9)
    }
}
