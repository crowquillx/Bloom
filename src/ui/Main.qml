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
    
    /// Sidebar overlay mode threshold (narrow screens use overlay)
    readonly property int overlayThreshold: 960
    
    /// Current content offset based on sidebar state
    readonly property int contentOffset: {
        if (!isLoggedIn) return 0
        if (sidebar.overlayMode) return 0
        return sidebar.sidebarWidth
    }

    VideoSurface {
        id: videoSurface
        anchors.fill: parent
        visible: PlayerController.supportsEmbeddedVideo && PlayerController.isPlaybackActive
        z: -1
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
        
        Behavior on anchors.leftMargin {
            NumberAnimation { 
                duration: SidebarSettings.reduceMotion ? 0 : Theme.durationNormal 
                easing.type: Easing.OutCubic
            }
        }
        
        // Handle Left arrow to focus sidebar when at left edge
        Keys.onLeftPressed: function(event) {
            if (isLoggedIn) {
                if (sidebar.expanded) {
                    // When sidebar is expanded, focus first nav item
                    sidebar.focusNavigation()
                    event.accepted = true
                } else if (!sidebar.overlayMode) {
                    // When sidebar is collapsed (rail mode), focus hamburger
                    sidebar.focusHamburger()
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
            if (activeFocus && stackView.currentItem && !stackView.currentItem.activeFocus) {
                console.log("[FocusDebug] mainContentArea got focus, forwarding to currentItem")
                Qt.callLater(() => stackView.currentItem.forceActiveFocus())
            }
        }
        
        StackView {
            id: stackView
            anchors.fill: parent
            initialItem: "LoginScreen.qml"
            
            onCurrentItemChanged: {
                console.log("[FocusDebug] StackView.currentItemChanged:", currentItem ? currentItem.toString() : "null", "sidebar.expanded:", sidebar.expanded)
                // Let screens with restoreFocusState handle their own focus restoration
                // This allows HomeScreen to restore focus to the previously selected item
                if (currentItem && !sidebar.expanded) {
                    if (currentItem.restoreFocusState) {
                        console.log("[FocusDebug] Screen has restoreFocusState, letting it handle focus")
                        // Don't force focus here - let StackView.onStatusChanged in the screen handle it
                    } else {
                        console.log("[FocusDebug] Setting focus to currentItem (no restoreFocusState)")
                        currentItem.forceActiveFocus()
                    }
                } else if (currentItem && sidebar.expanded) {
                    console.log("[FocusDebug] Skipping focus - sidebar is expanded, will restore later")
                }
                updateSidebarNavigation()
            }
        }
    }
    
    // ========================================
    // Sidebar Navigation Shell
    // ========================================
    
    Sidebar {
        id: sidebar
        anchors.fill: parent
        visible: isLoggedIn
        overlayMode: window.width < overlayThreshold
        currentNavigation: "home"
        mainContent: mainContentArea  // Connect to main content for focus navigation

        onNavigationRequested: function(navigationId) {
            playPointerSelectSound()
            // Handle navigation requests
            switch (navigationId) {
                case "home":
                    // Save focus state before navigating
                    var homeScreen = stackView.find(function(item) { return item && item.navigationId === "home" })
                    if (homeScreen) homeScreen.saveFocusState()
                    // Pop back to home screen
                    while (stackView.depth > 1) {
                        stackView.pop()
                    }
                    break
                case "search":
                    // Save focus state before navigating
                    var homeForSearch = stackView.find(function(item) { return item && item.navigationId === "home" })
                    if (homeForSearch) homeForSearch.saveFocusState()
                    // Navigate to search screen
                    pushSearchScreen()
                    break
                case "settings":
                    // Save focus state before navigating
                    var homeForSettings = stackView.find(function(item) { return item && item.navigationId === "home" })
                    if (homeForSettings) homeForSettings.saveFocusState()
                    // Navigate to settings screen
                    pushSettingsScreen()
                    break
            }
        }

        onLibraryRequested: function(libraryId, libraryName) {
            playPointerSelectSound()
            if (!libraryId)
                return
            // Save focus state before navigating
            var homeScreenForLibrary = stackView.find(function(item) { return item && item.navigationId === "home" })
            if (homeScreenForLibrary) homeScreenForLibrary.saveFocusState()
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
    
    // Function to push settings screen and wire up signals
    function updateSidebarNavigation() {
        var item = stackView.currentItem
        var navigationId = (item && item.navigationId) ? item.navigationId : "home"
        sidebar.currentNavigation = navigationId
        if (navigationId && navigationId.indexOf("library:") === 0) {
            sidebar.currentLibraryId = navigationId.substring("library:".length)
        } else {
            sidebar.currentLibraryId = ""
        }
    }

    function playPointerSelectSound() {
        if (InputModeManager.pointerActive) {
            UiSoundController.playSelect()
        }
    }

    function pushSettingsScreen() {
        playPointerSelectSound()
        stackView.push("SettingsScreen.qml")
        Qt.callLater(function() {
            var settingsScreen = stackView.currentItem
            if (settingsScreen && settingsScreen.signOutRequested) {
                settingsScreen.signOutRequested.connect(function() {
                    AuthenticationService.logout()
                })
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
                    currentLibraryName: libraryName
                })
                Qt.callLater(function() {
                    var screen = stackView.currentItem
                    if (screen && screen.showMovieDetailsView) {
                        screen.showMovieDetailsView(movieData)
                    }
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
                    showSeriesDetails: true
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
                        directNavigationMode: true
                    })
                    // Defer calling showMovieDetailsView until screen is ready
                    Qt.callLater(function() {
                        var screen = stackView.currentItem
                        if (screen && screen.showMovieDetailsView) {
                            screen.showMovieDetailsView(movieData)
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
                        directNavigationMode: true
                    })
                    // Need to load series details first for context, then show episode
                    LibraryService.getSeriesDetails(seriesId)
                    // Defer calling showEpisodeDetails until screen is ready
                    Qt.callLater(function() {
                        var screen = stackView.currentItem
                        if (screen && screen.showEpisodeDetails) {
                            screen.showEpisodeDetails(episodeData)
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
                        directNavigationMode: true
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
                        directNavigationMode: true
                    })
                    // Load series details, seasons, and next episode
                    LibraryService.getSeriesDetails(seriesId)
                    LibraryService.getItems(seriesId, 0, 0)  // Load seasons
                    LibraryService.getNextUnplayedEpisode(seriesId)
                })
            }
            updateSidebarNavigation()
        }
        
        function onLoggedOut() {
            console.log("Main.qml: Logged out, returning to login screen")
            
            // Mark as logged out to hide sidebar
            window.isLoggedIn = false
            
            // Close sidebar if open
            sidebar.close()
            
            // Clear all screens and go back to login
            stackView.clear()
            stackView.push("LoginScreen.qml")
        }
        
        ignoreUnknownSignals: true
    }
    
    // Connection to handle post-playback navigation to next episode
    Connections {
        target: PlayerController
        
        function onNavigateToNextEpisode(episodeData, seriesId, lastAudioIndex, lastSubtitleIndex) {
            console.log("[Main] Navigating to next episode after playback:", 
                        episodeData.SeriesName, "S" + episodeData.ParentIndexNumber + "E" + episodeData.IndexNumber,
                        "Audio:", lastAudioIndex, "Subtitle:", lastSubtitleIndex)
            
            // Pop back to the root level (home screen) first, then navigate to the episode
            while (stackView.depth > 1) {
                stackView.pop(null)  // Pop without transition
            }
            
            // Navigate to the episode view via LibraryScreen
            // We use empty library info since we're coming from playback context
            stackView.push("LibraryScreen.qml", {
                currentParentId: "",
                currentLibraryId: "",
                currentLibraryName: "",
                currentSeriesId: seriesId,
                directNavigationMode: true,
                // Pass the track preferences to be applied
                pendingAudioTrackIndex: lastAudioIndex,
                pendingSubtitleTrackIndex: lastSubtitleIndex
            })
            
            // Load series details for context
            LibraryService.getSeriesDetails(seriesId)
            
            // Defer calling showEpisodeDetails until screen is ready
            Qt.callLater(function() {
                var screen = stackView.currentItem
                if (screen && screen.showEpisodeDetails) {
                    screen.showEpisodeDetails(episodeData)
                }
            })
        }
        
        ignoreUnknownSignals: true
    }

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: Qt.quit()
    }

    Shortcut {
        sequences: ["Esc", "Backspace"]
        enabled: stackView.depth > 1 && !sidebar.expanded && !(stackView.currentItem && stackView.currentItem.handlesOwnBackNavigation === true)
        onActivated: {
            console.log("[FocusDebug] Back shortcut activated, stackView.depth:", stackView.depth, "sidebar.expanded:", sidebar.expanded)
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
        onActivated: sidebar.toggle()
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
}
