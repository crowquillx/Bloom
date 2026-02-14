import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BloomUI

/**
 * Sidebar - A glassmorphic, accessible navigation sidebar
 * 
 * Features:
 * - Collapsed rail (64px) with icons and tooltips
 * - Expanded panel (260px) with icons and labels
 * - Hamburger toggle button fixed at top-left
 * - Full keyboard/DPAD navigation: Enter/Space toggle, ESC close, arrow keys
 * - Overlay mode on narrow screens (<960px), push mode on wider screens
 * - Focus trap when open (modal behavior)
 * - State persistence via SidebarSettings (QSettings)
 * - Respects reduced-motion preference
 * - Glassmorphic design following Theme system
 */
Item {
    id: root
    
    // ========================================
    // Public API
    // ========================================
    
    /// Whether the sidebar is expanded (panel) or collapsed (rail)
    property bool expanded: SidebarSettings.sidebarExpanded
    
    /// Whether to use overlay mode (true) or push mode (false)
    /// Defaults from Theme.sidebarDefaultMode ("overlay" or "push"), but can be overridden
    property bool overlayMode: Theme.sidebarDefaultMode === "overlay"
    
    /// Navigation items for the sidebar - using direct icon references
    /// to ensure they're evaluated at render time
    property var navigationItems: [
        { id: "home", icon: Icons.home, label: qsTr("Home"), tooltip: qsTr("Home") },
        { id: "search", icon: Icons.search, label: qsTr("Search"), tooltip: qsTr("Search movies and shows") }
    ]

    // Library data and ordering helpers
    property var orderedLibraryItems: []
    function computeOrderedLibraries(views) {
        var order = SidebarSettings.libraryOrder || []
        var byId = {}
        var ordered = []
        for (var i = 0; i < views.length; i++) {
            byId[views[i].Id] = views[i]
        }

        // Add libraries in saved order first
        for (var j = 0; j < order.length; j++) {
            var id = order[j]
            if (byId[id]) {
                ordered.push(byId[id])
                delete byId[id]
            }
        }

        // Append any new/unsaved libraries
        var remainingIds = Object.keys(byId)
        for (var k = 0; k < remainingIds.length; k++) {
            ordered.push(byId[remainingIds[k]])
        }

        // Persist if the effective order differs (adds new libraries)
        var newOrderIds = ordered.map(function (v) { return v.Id })
        if (order.length !== newOrderIds.length || JSON.stringify(order) !== JSON.stringify(newOrderIds)) {
            SidebarSettings.libraryOrder = newOrderIds
        }

        return ordered
    }

    function syncLibraryItems() {
        var views = LibraryViewModel.views || []
        orderedLibraryItems = computeOrderedLibraries(views)
        libraryItems = orderedLibraryItems
        if (reorderingIndex >= libraryItems.length) {
            reorderingIndex = libraryItems.length - 1
        }
    }
    
    /// Current active navigation item
    property string currentNavigation: "home"
    /// Current active library id (derived from currentNavigation when applicable)
    property string currentLibraryId: ""

    // Library ordering state
    property var libraryItems: []
    property int reorderingIndex: -1
    property bool reorderModeActive: reorderingIndex >= 0
    readonly property int reorderHoldDuration: 500
    
    /// Signal emitted when a navigation item is selected
    signal navigationRequested(string navigationId)
    /// Signal emitted when a library is selected
    signal libraryRequested(string libraryId, string libraryName)
    
    /// Signal emitted when user requests to sign out
    signal signOutRequested()
    
    /// Signal emitted when user requests to exit the app
    signal exitRequested()
    
    // ========================================
    // Internal Properties
    // ========================================
    
    readonly property int collapsedWidth: Math.round(64 * Theme.layoutScale)
    readonly property int expandedWidth: Math.round(260 * Theme.layoutScale)
    readonly property int headerHeight: Math.round(64 * Theme.layoutScale)
    
    // Animation duration (respects reduced motion)
    readonly property int animDuration: SidebarSettings.reduceMotion ? 0 : Theme.durationNormal
    
    // Computed sidebar width
    readonly property int sidebarWidth: expanded ? expandedWidth : collapsedWidth
    
    // Whether sidebar should be visible at all (in overlay mode, only when expanded)
    readonly property bool sidebarVisible: !overlayMode || expanded
    
    // Reference to the window for width checks
    property var window: Window.window

    // Initialize library ordering
    Component.onCompleted: {
        syncLibraryItems()
    }

    Connections {
        target: LibraryViewModel
        function onViewsChanged() {
            syncLibraryItems()
        }
    }

    Connections {
        target: SidebarSettings
        function onLibraryOrderChanged() {
            syncLibraryItems()
        }
    }
    
    // ========================================
    // Public Functions
    // ========================================
    
    /// Reference to the main content for focus return
    property Item mainContent: null
    
    function toggle() {
        SidebarSettings.toggleSidebar()
    }
    
    function open() {
        SidebarSettings.sidebarExpanded = true
    }
    
    function close() {
        SidebarSettings.sidebarExpanded = false
        // Return focus to main content
        console.log("[FocusDebug] Sidebar.close() called, attempting to restore focus to mainContent")
        if (mainContent) {
            console.log("[FocusDebug] mainContent exists, calling forceActiveFocus()")
            mainContent.forceActiveFocus()
            console.log("[FocusDebug] mainContent.activeFocus:", mainContent.activeFocus)
        } else {
            console.log("[FocusDebug] mainContent is null!")
        }
    }
    
    /// Focus the hamburger button (called from main content via Left arrow at edge)
    function focusHamburger() {
        hamburgerButton.forceActiveFocus()
    }
    
    /// Focus the navigation list (called from main content when sidebar is expanded)
    function focusNavigation() {
        if (expanded) {
            if (navListView.count > 0) {
                navListView.currentIndex = 0
                navListView.currentItem.forceActiveFocus()
            } else {
                sidebarHamburger.forceActiveFocus()
            }
        }
    }

    function reorderLibrary(fromIndex, toIndex) {
        if (fromIndex === toIndex || fromIndex < 0 || toIndex < 0)
            return
        if (fromIndex >= libraryItems.length || toIndex >= libraryItems.length)
            return
        var reordered = libraryItems.slice()
        var item = reordered.splice(fromIndex, 1)[0]
        reordered.splice(toIndex, 0, item)
        libraryItems = reordered
        orderedLibraryItems = reordered
        reorderingIndex = toIndex
        SidebarSettings.libraryOrder = reordered.map(function (v) { return v.Id })
        libraryListView.currentIndex = toIndex
        Qt.callLater(function() {
            if (libraryListView.currentItem) {
                libraryListView.currentItem.forceActiveFocus()
            }
        })
    }

    function finishReorder() {
        reorderingIndex = -1
    }
    
    // ========================================
    // Backdrop (Overlay Mode Only)
    // ========================================
    
    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        visible: overlayMode && expanded
        opacity: visible ? 1.0 : 0.0
        z: 99
        
        Behavior on opacity {
            NumberAnimation { duration: root.animDuration }
        }
        
        MouseArea {
            anchors.fill: parent
            enabled: backdrop.visible
            onClicked: root.close()
        }
    }
    
    // ========================================
    // Sidebar Panel
    // ========================================
    
    // Frosted glass background - captures and blurs the content behind
    Item {
        id: sidebarPanel
        width: root.sidebarWidth
        height: parent.height
        x: overlayMode ? (expanded ? 0 : -expandedWidth) : 0
        z: 100
        clip: true
        
        Behavior on width {
            NumberAnimation { duration: root.animDuration; easing.type: Easing.OutCubic }
        }
        
        Behavior on x {
            NumberAnimation { duration: root.animDuration; easing.type: Easing.OutCubic }
        }
        
        // Blur source - captures what's behind the sidebar
        ShaderEffectSource {
            id: blurSource
            sourceItem: root.mainContent
            sourceRect: Qt.rect(sidebarPanel.x, 0, sidebarPanel.width, sidebarPanel.height)
            visible: false
        }
        
        // Blurred background layer
        Item {
            id: blurredBackground
            anchors.fill: parent
            visible: !SidebarSettings.reduceMotion && root.mainContent !== null
            
            // The captured content
            ShaderEffectSource {
                id: capturedContent
                anchors.fill: parent
                sourceItem: root.mainContent
                sourceRect: Qt.rect(0, 0, sidebarPanel.width, sidebarPanel.height)
                visible: false
            }
            
            // Apply blur effect
            MultiEffect {
                anchors.fill: parent
                source: capturedContent
                blurEnabled: true
                blur: 1.0
                blurMax: 48
                blurMultiplier: 1.5
                saturation: -0.2
            }
        }
        
        // Glass overlay with tint
        Rectangle {
            id: glassOverlay
            anchors.fill: parent
            color: Qt.rgba(0.05, 0.05, 0.1, 0.75)
            
            // Subtle gradient for depth
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.08, 0.08, 0.12, 0.8) }
                GradientStop { position: 1.0; color: Qt.rgba(0.03, 0.03, 0.06, 0.85) }
            }
        }
        
        // Light border effect for glass edge
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: Qt.rgba(1, 1, 1, 0.1)
            border.width: 1
        }
        
        // Right edge highlight (separator from content)
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Qt.rgba(1, 1, 1, 0.15)
        }
        
        // ========================================
        // Sidebar Content
        // ========================================
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            
            // Header with Hamburger Button
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.headerHeight
                
                // Hamburger button (inside sidebar in expanded mode)
                Button {
                    id: sidebarHamburger
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(12 * Theme.layoutScale)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(40 * Theme.layoutScale)
                    height: Math.round(40 * Theme.layoutScale)
                    visible: expanded
                    
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: sidebarHamburger.activeFocus ? Theme.buttonSecondaryBackgroundHover 
                             : (sidebarHamburger.hovered ? Theme.buttonSecondaryBackgroundHover : "transparent")
                        border.color: sidebarHamburger.activeFocus ? Theme.focusBorder : "transparent"
                        border.width: sidebarHamburger.activeFocus ? 2 : 0
                    }
                    
                    contentItem: Text {
                        text: Icons.close
                        font.pixelSize: 24
                        font.family: Theme.fontIcon
                        font.weight: Font.Normal
                        color: Theme.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        renderType: Text.NativeRendering
                    }
                    
                    onClicked: root.close()
                    
                    Keys.onReturnPressed: root.close()
                    Keys.onEnterPressed: root.close()
                    Keys.onSpacePressed: root.close()
                    Keys.onEscapePressed: root.close()
                    
                    Keys.onDownPressed: {
                        if (navListView.count > 0) {
                            navListView.currentIndex = 0
                            navListView.currentItem.forceActiveFocus()
                        }
                    }
                    
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Close sidebar")
                    Accessible.description: qsTr("Press to close the navigation sidebar")
                }
                
                // Title (in expanded mode)
                Text {
                    anchors.left: sidebarHamburger.right
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Bloom"
                    font.pixelSize: Theme.fontSizeTitle
                    font.family: Theme.fontPrimary
                    font.bold: true
                    color: Theme.textPrimary
                    visible: expanded
                    opacity: expanded ? 1.0 : 0.0
                    
                    Behavior on opacity {
                        NumberAnimation { duration: root.animDuration }
                    }
                }
            }
            
            // Separator
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                color: Theme.borderLight
            }
            
            // Navigation Items (static)
            ListView {
                id: navListView
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                Layout.topMargin: 8
                model: root.navigationItems
                clip: true
                interactive: false
                spacing: 4
                
                // Focus handling - we handle key navigation manually in delegates
                focus: false
                keyNavigationEnabled: false
                
                delegate: FocusScope {
                    id: navDelegateScope
                    width: navListView.width
                    height: Math.round(56 * Theme.layoutScale)
                    
                    required property var modelData
                    required property int index
                    
                    property bool isActive: root.currentNavigation === modelData.id
                    property bool isFocused: activeFocus
                    
                    // Key handling on the FocusScope level
                    Keys.onReturnPressed: (event) => {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        navDelegate.clicked()
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        navDelegate.clicked()
                        event.accepted = true
                    }
                    Keys.onSpacePressed: (event) => {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        navDelegate.clicked()
                        event.accepted = true
                    }
                    
                    Keys.onUpPressed: {
                        if (index > 0) {
                            navListView.currentIndex = index - 1
                            navListView.currentItem.forceActiveFocus()
                        } else {
                            // At first item - navigate to hamburger/close button
                            if (expanded) {
                                sidebarHamburger.forceActiveFocus()
                            } else if (!overlayMode) {
                                // Collapsed rail mode - focus external hamburger
                                hamburgerButton.forceActiveFocus()
                            }
                        }
                    }
                    Keys.onDownPressed: {
                        if (index < navListView.count - 1) {
                            navListView.currentIndex = index + 1
                            navListView.currentItem.forceActiveFocus()
                        } else {
                            // Move into libraries if present, otherwise to sign out
                            if (libraryListView.count > 0) {
                                libraryListView.currentIndex = 0
                                libraryListView.currentItem.forceActiveFocus()
                            } else {
                                signOutButton.forceActiveFocus()
                            }
                        }
                    }
                    Keys.onRightPressed: {
                        // Navigate to main content
                        console.log("[FocusDebug] Nav delegate Right pressed, mainContent:", root.mainContent ? "exists" : "null")
                        if (root.mainContent) {
                            root.mainContent.forceActiveFocus()
                            console.log("[FocusDebug] After forceActiveFocus, mainContent.activeFocus:", root.mainContent.activeFocus)
                        }
                    }
                    Keys.onEscapePressed: root.close()
                    
                    ItemDelegate {
                        id: navDelegate
                        anchors.fill: parent
                        
                        // Glassmorphic background
                        background: Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSmall
                            anchors.rightMargin: Theme.spacingSmall
                            radius: Theme.radiusMedium
                            color: {
                                if (navDelegateScope.isFocused) return Theme.buttonSecondaryBackgroundPressed
                                if (navDelegateScope.isActive) return Theme.buttonTabBackgroundActive
                                if (navDelegate.hovered) return Theme.buttonSecondaryBackgroundHover
                                return "transparent"
                            }
                            border.color: {
                                if (navDelegateScope.isFocused) return Theme.focusBorder
                                if (navDelegateScope.isActive) return Theme.accentSecondary
                                return "transparent"
                            }
                            border.width: navDelegateScope.isFocused ? 2 : (navDelegateScope.isActive ? 1 : 0)
                            
                            Behavior on color { ColorAnimation { duration: 100 } }
                            Behavior on border.color { ColorAnimation { duration: 100 } }
                        }
                        
                        contentItem: RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: expanded ? Theme.spacingMedium : 0
                            anchors.rightMargin: expanded ? Theme.spacingSmall * 2 : 0
                            spacing: expanded ? Theme.spacingSmall * 2 : 0
                            
                            // Icon container for proper centering
                            Item {
                                Layout.preferredWidth: expanded ? Math.round(24 * Theme.layoutScale) : parent.width
                                Layout.preferredHeight: Math.round(24 * Theme.layoutScale)
                                Layout.alignment: Qt.AlignVCenter
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: navDelegateScope.modelData.icon
                                    font.pixelSize: Theme.fontSizeIcon
                                    font.family: Theme.fontIcon
                                    color: navDelegateScope.isActive ? Theme.accentPrimary : Theme.textPrimary
                                    renderType: Text.NativeRendering
                                    
                                    Behavior on color { ColorAnimation { duration: 100 } }
                                }
                            }
                            
                            // Label (visible when expanded)
                            Text {
                                text: navDelegateScope.modelData.label
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                font.weight: navDelegateScope.isActive ? Font.DemiBold : Font.Normal
                                color: navDelegateScope.isActive ? Theme.textPrimary : Theme.textSecondary
                                visible: expanded
                                opacity: expanded ? 1.0 : 0.0
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                
                                Behavior on opacity {
                                    NumberAnimation { duration: root.animDuration }
                                }
                                Behavior on color { ColorAnimation { duration: 100 } }
                            }
                        }
                        
                        // Tooltip for collapsed mode
                        ToolTip.visible: !expanded && hovered
                        ToolTip.text: navDelegateScope.modelData.tooltip
                        ToolTip.delay: 500
                        
                        onClicked: {
                            root.currentNavigation = navDelegateScope.modelData.id
                            root.navigationRequested(navDelegateScope.modelData.id)
                            if (root.overlayMode) {
                                root.close()
                            }
                        }
                        
                        Accessible.role: Accessible.MenuItem
                        Accessible.name: navDelegateScope.modelData.label
                        Accessible.description: navDelegateScope.modelData.tooltip
                    }
                }
            }

            // Media Section Header
            Text {
                id: mediaHeader
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 16
                Layout.topMargin: 12
                text: qsTr("Media")
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
                visible: expanded && libraryListView.count > 0
                opacity: expanded ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: root.animDuration } }
            }

            // Library Items
            ListView {
                id: libraryListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: expanded ? 4 : 0
                model: root.libraryItems
                clip: true
                interactive: false
                spacing: 4
                currentIndex: 0
                footer: Item {
                    width: libraryListView.width
                    height: (expanded && libraryListView.count > 0) ? (reorderTip.implicitHeight + Theme.spacingSmall) : 0
                    visible: expanded && libraryListView.count > 0

                    Text {
                        id: reorderTip
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        text: qsTr("Tip: Shift+Enter to reorder libraries, then use Up/Down.")
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        opacity: 0.8
                    }
                }

                delegate: FocusScope {
                    id: libraryDelegateScope
                    width: libraryListView.width
                    height: Math.round(56 * Theme.layoutScale)

                    required property var modelData
                    required property int index

                    property bool isActive: root.currentNavigation === ("library:" + (modelData.Id || ""))
                    property bool isFocused: activeFocus
                    property bool isReordering: root.reorderModeActive && root.reorderingIndex === index
                    Keys.priority: Keys.BeforeItem

                    function activateLibrary() {
                        root.currentNavigation = "library:" + (modelData.Id || "")
                        root.currentLibraryId = modelData.Id || ""
                        root.libraryRequested(modelData.Id || "", modelData.Name || "")
                        if (root.overlayMode) {
                            root.close()
                        }
                    }

                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            if (event.isAutoRepeat) {
                                event.accepted = true
                                return
                            }
                            if (event.modifiers & Qt.ShiftModifier) {
                                // Toggle reorder mode on this item
                                if (root.reorderModeActive && root.reorderingIndex === index) {
                                    root.finishReorder()
                                } else {
                                    root.reorderingIndex = index
                                }
                                event.accepted = true
                                return
                            }

                            if (root.reorderModeActive) {
                                root.finishReorder()
                                event.accepted = true
                                return
                            }

                            activateLibrary()
                            event.accepted = true
                            return
                        }
                    }

                    Keys.onUpPressed: function(event) {
                        if (root.reorderModeActive && root.reorderingIndex === index) {
                            var target = Math.max(0, index - 1)
                            root.reorderLibrary(index, target)
                            event.accepted = true
                        } else if (root.reorderModeActive) {
                            event.accepted = true
                        } else if (index > 0) {
                            libraryListView.currentIndex = index - 1
                            libraryListView.currentItem.forceActiveFocus()
                            event.accepted = true
                        } else {
                            if (navListView.count > 0) {
                                navListView.currentIndex = navListView.count - 1
                                navListView.currentItem.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                    }

                    Keys.onDownPressed: function(event) {
                        if (root.reorderModeActive && root.reorderingIndex === index) {
                            var target = Math.min(libraryListView.count - 1, index + 1)
                            root.reorderLibrary(index, target)
                            event.accepted = true
                        } else if (root.reorderModeActive) {
                            event.accepted = true
                        } else if (index < libraryListView.count - 1) {
                            libraryListView.currentIndex = index + 1
                            libraryListView.currentItem.forceActiveFocus()
                            event.accepted = true
                        } else {
                            settingsButton.forceActiveFocus()
                            event.accepted = true
                        }
                    }

                    Keys.onRightPressed: function(event) {
                        if (root.mainContent) {
                            console.log("[FocusDebug] libraryListView delegate Right pressed, setting focus to mainContent")
                            root.mainContent.forceActiveFocus()
                            event.accepted = true
                        }
                    }

                    Keys.onEscapePressed: function(event) {
                        if (root.reorderModeActive) {
                            root.finishReorder()
                        } else {
                            root.close()
                        }
                        event.accepted = true
                    }

                    Keys.onSpacePressed: {
                        if (root.reorderModeActive && root.reorderingIndex === index) {
                            root.finishReorder()
                        } else {
                            activateLibrary()
                        }
                    }

                    ItemDelegate {
                        id: libraryDelegate
                        anchors.fill: parent
                        background: Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSmall
                            anchors.rightMargin: Theme.spacingSmall
                            radius: Theme.radiusMedium
                            color: {
                                if (libraryDelegateScope.isReordering) return Theme.buttonSecondaryBackgroundPressed
                                if (libraryDelegateScope.isFocused) return Theme.buttonSecondaryBackgroundPressed
                                if (libraryDelegateScope.isActive) return Theme.buttonTabBackgroundActive
                                if (libraryDelegate.hovered) return Theme.buttonSecondaryBackgroundHover
                                return "transparent"
                            }
                            border.color: {
                                if (libraryDelegateScope.isReordering) return Theme.accentSecondary
                                if (libraryDelegateScope.isFocused) return Theme.focusBorder
                                if (libraryDelegateScope.isActive) return Theme.accentSecondary
                                return "transparent"
                            }
                            border.width: libraryDelegateScope.isFocused || libraryDelegateScope.isReordering ? 2 : (libraryDelegateScope.isActive ? 1 : 0)

                            Behavior on color { ColorAnimation { duration: 100 } }
                            Behavior on border.color { ColorAnimation { duration: 100 } }
                        }

                        contentItem: RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: expanded ? Theme.spacingMedium : 0
                            anchors.rightMargin: expanded ? Theme.spacingSmall * 2 : 0
                            spacing: expanded ? Theme.spacingSmall * 2 : 0

                            Item {
                                Layout.preferredWidth: expanded ? Math.round(24 * Theme.layoutScale) : parent.width
                                Layout.preferredHeight: Math.round(24 * Theme.layoutScale)
                                Layout.alignment: Qt.AlignVCenter

                                Text {
                                    anchors.centerIn: parent
                                    text: {
                                        var collectionType = (libraryDelegateScope.modelData.CollectionType || "").toLowerCase()
                                        if (collectionType === "tvshows") return Icons.tvShows
                                        if (collectionType === "movies") return Icons.movie
                                        if (collectionType === "music") return Icons.music
                                        return Icons.folder
                                    }
                                    font.pixelSize: Theme.fontSizeIcon
                                    font.family: Theme.fontIcon
                                    color: libraryDelegateScope.isActive ? Theme.accentPrimary : Theme.textPrimary
                                    renderType: Text.NativeRendering
                                    Behavior on color { ColorAnimation { duration: 100 } }
                                }
                            }

                            Text {
                                text: libraryDelegateScope.modelData.Name
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                font.weight: libraryDelegateScope.isActive ? Font.DemiBold : Font.Normal
                                color: libraryDelegateScope.isActive ? Theme.textPrimary : Theme.textSecondary
                                visible: expanded
                                opacity: expanded ? 1.0 : 0.0
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                elide: Text.ElideRight

                                Behavior on opacity { NumberAnimation { duration: root.animDuration } }
                                Behavior on color { ColorAnimation { duration: 100 } }
                            }

                            // Reorder indicator
                            Text {
                                text: libraryDelegateScope.isReordering ? "↕" : ""
                                visible: expanded && libraryDelegateScope.isReordering
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.accentSecondary
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }

                        ToolTip.visible: !expanded && hovered
                        ToolTip.text: libraryDelegateScope.modelData.Name
                        ToolTip.delay: 500

                        onClicked: {
                            if (libraryDelegateScope.suppressClickOnce) {
                                libraryDelegateScope.suppressClickOnce = false
                                return
                            }
                            if (root.reorderModeActive && root.reorderingIndex === index) {
                                root.finishReorder()
                                return
                            }
                            libraryDelegateScope.activateLibrary()
                        }

                        Accessible.role: Accessible.MenuItem
                        Accessible.name: libraryDelegateScope.modelData.Name
                        Accessible.description: "Open library " + libraryDelegateScope.modelData.Name
                    }
                }
            }

            // Spacer
            Item { Layout.fillHeight: true }
            
            // ========================================
            // Settings + Sign Out + Exit (at bottom)
            // ========================================
            
            // Separator above settings/sign out
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.leftMargin: Math.round(12 * Theme.layoutScale)
                Layout.rightMargin: Math.round(12 * Theme.layoutScale)
                color: Theme.borderLight
                visible: expanded || !overlayMode
            }
            
            // Settings button (now placed above Sign Out)
            ItemDelegate {
                id: settingsButton
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(56 * Theme.layoutScale)
                visible: expanded || !overlayMode
                
                background: Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    radius: Theme.radiusMedium
                    color: {
                        if (settingsButton.activeFocus) return Theme.buttonSecondaryBackgroundPressed
                        if (root.currentNavigation === "settings") return Theme.buttonTabBackgroundActive
                        if (settingsButton.hovered) return Theme.buttonSecondaryBackgroundHover
                        return "transparent"
                    }
                    border.color: settingsButton.activeFocus ? Theme.focusBorder : "transparent"
                    border.width: settingsButton.activeFocus ? 2 : (root.currentNavigation === "settings" ? 1 : 0)
                    
                    Behavior on color { ColorAnimation { duration: 100 } }
                    Behavior on border.color { ColorAnimation { duration: 100 } }
                }
                
                contentItem: RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: expanded ? Theme.spacingMedium : 0
                    anchors.rightMargin: expanded ? Theme.spacingSmall * 2 : 0
                    spacing: expanded ? Theme.spacingSmall * 2 : 0
                    
                    Item {
                        Layout.preferredWidth: expanded ? Math.round(24 * Theme.layoutScale) : parent.width
                        Layout.preferredHeight: Math.round(24 * Theme.layoutScale)
                        Layout.alignment: Qt.AlignVCenter
                        
                        Text {
                            anchors.centerIn: parent
                            text: Icons.settings
                            font.pixelSize: Theme.fontSizeIcon
                            font.family: Theme.fontIcon
                            color: root.currentNavigation === "settings" ? Theme.accentPrimary : Theme.textPrimary
                            renderType: Text.NativeRendering
                        }
                    }
                    
                    Text {
                        text: "Settings"
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        font.weight: root.currentNavigation === "settings" ? Font.DemiBold : Font.Normal
                        color: root.currentNavigation === "settings" ? Theme.textPrimary : Theme.textSecondary
                        visible: expanded
                        opacity: expanded ? 1.0 : 0.0
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        
                        Behavior on opacity {
                            NumberAnimation { duration: root.animDuration }
                        }
                    }
                }
                
                ToolTip.visible: !expanded && hovered
                ToolTip.text: "Settings"
                ToolTip.delay: 500
                
                onClicked: {
                    root.currentNavigation = "settings"
                    root.navigationRequested("settings")
                    if (root.overlayMode) {
                        root.close()
                    }
                }
                
                Keys.onReturnPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                Keys.onEnterPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                Keys.onSpacePressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                
                Keys.onUpPressed: {
                    if (libraryListView.count > 0) {
                        libraryListView.currentIndex = libraryListView.count - 1
                        libraryListView.currentItem.forceActiveFocus()
                    } else if (navListView.count > 0) {
                        navListView.currentIndex = navListView.count - 1
                        navListView.currentItem.forceActiveFocus()
                    }
                }
                Keys.onDownPressed: {
                    signOutButton.forceActiveFocus()
                }
                Keys.onRightPressed: {
                    if (root.mainContent) {
                        root.mainContent.forceActiveFocus()
                    }
                }
                Keys.onEscapePressed: root.close()
                
                Accessible.role: Accessible.Button
                Accessible.name: "Settings"
                Accessible.description: "Open settings"
            }
            
            // Sign out button
            ItemDelegate {
                id: signOutButton
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(56 * Theme.layoutScale)
                visible: expanded || !overlayMode
                
                background: Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    radius: Theme.radiusMedium
                    color: {
                        if (signOutButton.activeFocus) return Theme.buttonSecondaryBackgroundPressed
                        if (signOutButton.hovered) return Theme.buttonSecondaryBackgroundHover
                        return "transparent"
                    }
                    border.color: signOutButton.activeFocus ? Theme.focusBorder : "transparent"
                    border.width: signOutButton.activeFocus ? 2 : 0
                    
                    Behavior on color { ColorAnimation { duration: 100 } }
                    Behavior on border.color { ColorAnimation { duration: 100 } }
                }
                
                contentItem: RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: expanded ? Theme.spacingMedium : 0
                    anchors.rightMargin: expanded ? Theme.spacingSmall * 2 : 0
                    spacing: expanded ? Theme.spacingSmall * 2 : 0
                    
                    // Icon container for proper centering
                    Item {
                        Layout.preferredWidth: expanded ? Math.round(24 * Theme.layoutScale) : parent.width
                        Layout.preferredHeight: Math.round(24 * Theme.layoutScale)
                        Layout.alignment: Qt.AlignVCenter
                        
                        Text {
                            anchors.centerIn: parent
                            text: Icons.logout
                            font.pixelSize: Theme.fontSizeIcon
                            font.family: Theme.fontIcon
                            color: Theme.textSecondary
                            renderType: Text.NativeRendering
                        }
                    }
                    
                    // Label (visible when expanded)
                    Text {
                        text: "Sign Out"
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        font.weight: Font.Normal
                        color: Theme.textSecondary
                        visible: expanded
                        opacity: expanded ? 1.0 : 0.0
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        
                        Behavior on opacity {
                            NumberAnimation { duration: root.animDuration }
                        }
                    }
                }
                
                // Tooltip for collapsed mode
                ToolTip.visible: !expanded && hovered
                ToolTip.text: "Sign out"
                ToolTip.delay: 500
                
                onClicked: {
                    root.signOutRequested()
                    if (root.overlayMode) {
                        root.close()
                    }
                }
                
                Keys.onReturnPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                Keys.onEnterPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                Keys.onSpacePressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                
                // DPAD navigation
                Keys.onUpPressed: {
                    settingsButton.forceActiveFocus()
                }
                Keys.onDownPressed: {
                    exitButton.forceActiveFocus()
                }
                Keys.onRightPressed: {
                    if (root.mainContent) {
                        root.mainContent.forceActiveFocus()
                    }
                }
                Keys.onEscapePressed: root.close()
                
                Accessible.role: Accessible.Button
                Accessible.name: "Sign out"
                Accessible.description: "Sign out from Jellyfin server"
            }
            
            // ========================================
            // Exit Button (at bottom of sidebar)
            // ========================================
            
            // Separator above exit
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.leftMargin: Math.round(12 * Theme.layoutScale)
                Layout.rightMargin: Math.round(12 * Theme.layoutScale)
                color: Theme.borderLight
                visible: expanded || !overlayMode
            }
            
            // Exit button
            ItemDelegate {
                id: exitButton
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(56 * Theme.layoutScale)
                visible: expanded || !overlayMode
                
                background: Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    radius: Theme.radiusMedium
                    color: {
                        if (exitButton.activeFocus) return Theme.buttonSecondaryBackgroundPressed
                        if (exitButton.hovered) return Theme.buttonSecondaryBackgroundHover
                        return "transparent"
                    }
                    border.color: exitButton.activeFocus ? Theme.focusBorder : "transparent"
                    border.width: exitButton.activeFocus ? 2 : 0
                    
                    Behavior on color { ColorAnimation { duration: 100 } }
                    Behavior on border.color { ColorAnimation { duration: 100 } }
                }
                
                contentItem: RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: expanded ? Theme.spacingMedium : 0
                    anchors.rightMargin: expanded ? Theme.spacingSmall * 2 : 0
                    spacing: expanded ? Theme.spacingSmall * 2 : 0
                    
                    // Icon container for proper centering
                    Item {
                        Layout.preferredWidth: expanded ? Math.round(24 * Theme.layoutScale) : parent.width
                        Layout.preferredHeight: Math.round(24 * Theme.layoutScale)
                        Layout.alignment: Qt.AlignVCenter
                        
                        Text {
                            anchors.centerIn: parent
                            text: Icons.power
                            font.pixelSize: Theme.fontSizeIcon
                            font.family: Theme.fontIcon
                            color: Theme.textSecondary
                            renderType: Text.NativeRendering
                        }
                    }
                    
                    // Label (visible when expanded)
                    Text {
                        text: "Exit"
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        font.weight: Font.Normal
                        color: Theme.textSecondary
                        visible: expanded
                        opacity: expanded ? 1.0 : 0.0
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        
                        Behavior on opacity {
                            NumberAnimation { duration: root.animDuration }
                        }
                    }
                }
                
                // Tooltip for collapsed mode
                ToolTip.visible: !expanded && hovered
                ToolTip.text: "Exit"
                ToolTip.delay: 500
                
                onClicked: {
                    root.exitRequested()
                    if (root.overlayMode) {
                        root.close()
                    }
                }
                
                Keys.onReturnPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                Keys.onEnterPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                Keys.onSpacePressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                
                // DPAD navigation
                Keys.onUpPressed: {
                    signOutButton.forceActiveFocus()
                }
                Keys.onRightPressed: {
                    if (root.mainContent) {
                        root.mainContent.forceActiveFocus()
                    }
                }
                Keys.onEscapePressed: root.close()
                
                Accessible.role: Accessible.Button
                Accessible.name: "Exit"
                Accessible.description: "Exit the application"
            }
            
            // Bottom padding
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.spacingSmall
            }
            
            // Bottom section (collapse button in rail mode)
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(56 * Theme.layoutScale)
                visible: !expanded && !overlayMode
                
                Button {
                    id: expandButton
                    anchors.centerIn: parent
                    width: Math.round(40 * Theme.layoutScale)
                    height: Math.round(40 * Theme.layoutScale)
                    
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: expandButton.activeFocus ? Theme.buttonSecondaryBackgroundHover 
                             : (expandButton.hovered ? Theme.buttonSecondaryBackgroundHover : "transparent")
                        border.color: expandButton.activeFocus ? Theme.focusBorder : "transparent"
                        border.width: expandButton.activeFocus ? 2 : 0
                    }
                    
                    contentItem: Text {
                        text: Icons.menu
                        font.pixelSize: Theme.fontSizeIcon
                        font.family: Theme.fontIcon
                        color: Theme.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: root.open()
                    
                    ToolTip.visible: hovered
                    ToolTip.text: "Expand sidebar"
                    ToolTip.delay: 500
                    
                    Accessible.role: Accessible.Button
                    Accessible.name: "Expand sidebar"
                }
            }
        }
    }
    
    // ========================================
    // Hamburger Button (Fixed Top-Left, outside sidebar)
    // ========================================
    
    Button {
        id: hamburgerButton
        x: overlayMode ? Math.round(12 * Theme.layoutScale) : (expanded ? Math.round(-100 * Theme.layoutScale) : Math.round(12 * Theme.layoutScale))
        y: Math.round(12 * Theme.layoutScale)
        width: Math.round(40 * Theme.layoutScale)
        height: Math.round(40 * Theme.layoutScale)
        z: 101
        visible: overlayMode || !expanded
        
        Behavior on x {
            NumberAnimation { duration: root.animDuration; easing.type: Easing.OutCubic }
        }
        
        background: Rectangle {
            radius: Theme.radiusSmall
            color: {
                if (hamburgerButton.activeFocus) return Theme.buttonSecondaryBackgroundPressed
                if (hamburgerButton.hovered) return Theme.buttonSecondaryBackgroundHover
                return Theme.buttonSecondaryBackground
            }
            border.color: hamburgerButton.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
            border.width: hamburgerButton.activeFocus ? 2 : 1
            
            Behavior on color { ColorAnimation { duration: 100 } }
            Behavior on border.color { ColorAnimation { duration: 100 } }
        }
        
        contentItem: Text {
            text: Icons.menu
            font.pixelSize: Theme.fontSizeIcon
            font.family: Theme.fontIcon
            color: Theme.textPrimary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        
        onClicked: root.toggle()
        
        Keys.onReturnPressed: root.toggle()
        Keys.onEnterPressed: root.toggle()
        Keys.onSpacePressed: root.toggle()
        Keys.onRightPressed: {
            // Navigate to main content
            console.log("[FocusDebug] hamburgerButton Right pressed")
            if (root.mainContent) {
                root.mainContent.forceActiveFocus()
                console.log("[FocusDebug] After forceActiveFocus on mainContent, activeFocus:", root.mainContent.activeFocus)
            }
        }
        Keys.onDownPressed: {
            // Navigate to first nav item in collapsed rail mode
            if (!expanded && !overlayMode && navListView.count > 0) {
                navListView.currentIndex = 0
                navListView.currentItem.forceActiveFocus()
            }
        }
        
        ToolTip.visible: hovered && !expanded
        ToolTip.text: expanded ? "Close sidebar" : "Open sidebar"
        ToolTip.delay: 500
        
        Accessible.role: Accessible.Button
        Accessible.name: expanded ? "Close navigation menu" : "Open navigation menu"
        Accessible.description: "Toggle the navigation sidebar"
    }
    
    // ========================================
    // Global Keyboard Handling
    // ========================================
    
    // ESC to close sidebar
    Shortcut {
        sequence: "Escape"
        enabled: expanded
        onActivated: root.close()
    }
    
    // Focus trap: when expanded, focus the sidebar content
    onExpandedChanged: {
        if (expanded) {
            // Focus the first nav item when sidebar expands
            Qt.callLater(function() {
                if (navListView.count > 0) {
                    navListView.currentIndex = 0
                    navListView.currentItem.forceActiveFocus()
                } else {
                    sidebarHamburger.forceActiveFocus()
                }
            })
        }
    }
    
    // ========================================
    // Accessibility
    // ========================================
    
    Accessible.role: Accessible.Pane
    Accessible.name: "Navigation sidebar"
    Accessible.description: expanded ? "Navigation sidebar is expanded" : "Navigation sidebar is collapsed"
}
