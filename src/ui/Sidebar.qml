import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * Sidebar - Bloom's floating navigation rail.
 *
 * - Floats inset from the window edges with rounded corners and a restrained
 *   translucent surface. Content reserves only the collapsed footprint
 *   (Theme.navRailReservedWidth); expansion overlays the page without moving,
 *   resizing or recropping it.
 * - Collapsed: large icons. Expanded: icons + labels. The rail expands while
 *   keyboard/gamepad focus is inside it, or while pinned open from the pointer
 *   toggle; hover is never required.
 * - The current page shows an accent indicator and tinted fill; the focused
 *   entry shows the shared high-contrast FocusRing.
 * - Up/Down walk every entry in order, Right/Escape return focus to the page,
 *   Enter/Space activate, Shift+Enter starts library reordering.
 * - Pointer clicks navigate directly without stealing keyboard focus.
 */
FocusScope {
    id: root

    // ========================================
    // Public API
    // ========================================

    /// Pinned open from the pointer toggle (labels visible without focus).
    property bool pinnedOpen: false

    /// Whether the rail is showing labels. True while focus is inside the
    /// rail or while pinned open.
    readonly property bool expanded: root.activeFocus || pinnedOpen

    /// Kept for API compatibility; the floating rail never pushes content.
    property bool overlayMode: false

    /// Navigation destinations above the library list.
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
            byId[views[i].itemId] = views[i]
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
        var newOrderIds = ordered.map(function (v) { return v.itemId })
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

    property bool _updatesReady: false
    property bool _updatesAvailable: false

    /// Signal emitted when a navigation item is selected
    signal navigationRequested(string navigationId)
    /// Signal emitted when a library is selected
    signal libraryRequested(string libraryId, string libraryName)
    /// Signal emitted when user requests to sign out
    signal signOutRequested()
    /// Signal emitted when user requests to exit the app
    signal exitRequested()

    /// Reference to the main content for focus return
    property Item mainContent: null

    // ========================================
    // Geometry
    // ========================================

    readonly property int collapsedWidth: Theme.navRailCollapsedWidth
    readonly property int expandedWidth: Theme.navRailExpandedWidth
    /// Width the page must reserve on its left edge. Constant: expansion
    /// overlays the content instead of pushing it.
    readonly property int sidebarWidth: Theme.navRailReservedWidth
    readonly property int railPadding: Math.round(12 * Theme.layoutScale)
    readonly property int animDuration: (SidebarSettings.reduceMotion || !Theme.uiAnimationsEnabled)
                                        ? 0 : Theme.durationNormal

    Component.onCompleted: syncLibraryItems()

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

    Timer {
        interval: 500
        repeat: false
        running: true
        onTriggered: {
            root._updatesReady = true
            if (typeof UpdateService !== "undefined")
                root._updatesAvailable = UpdateService.updateAvailable
        }
    }

    Connections {
        target: typeof UpdateService !== "undefined" ? UpdateService : null
        enabled: root._updatesReady
        function onUpdateAvailableChanged() {
            Qt.callLater(function() {
                root._updatesAvailable = UpdateService.updateAvailable
            })
        }
    }

    // ========================================
    // Public Functions
    // ========================================

    function toggle() {
        if (expanded) {
            close()
        } else {
            activateKeyboardNavigation()
            focusRail()
        }
    }

    function open() {
        focusRail()
    }

    function activateKeyboardNavigation() {
        if (typeof InputModeManager === "undefined")
            return
        InputModeManager.setNavigationMode("keyboard")
        InputModeManager.hideCursor(true)
    }

    function close() {
        var hadFocus = root.activeFocus
        pinnedOpen = false
        finishReorder()
        if (!hadFocus)
            return
        Qt.callLater(restoreMainContentFocus)
    }

    function restoreMainContentFocus() {
        if (!mainContent)
            return
        if (typeof mainContent.restoreFocusFromSidebar === "function") {
            mainContent.restoreFocusFromSidebar()
        } else {
            mainContent.forceActiveFocus()
        }
    }

    function handleNavigation(navigationId) {
        var hadFocus = root.activeFocus
        pinnedOpen = false
        currentNavigation = navigationId
        if (navigationId.indexOf("library:") !== 0) {
            currentLibraryId = ""
        }
        navigationRequested(navigationId)
        if (hadFocus)
            Qt.callLater(restoreMainContentFocus)
    }

    function activateLibrary(index) {
        var library = libraryItems[index]
        if (!library)
            return
        var hadFocus = root.activeFocus
        pinnedOpen = false
        currentNavigation = "library:" + (library.itemId || "")
        currentLibraryId = library.itemId || ""
        libraryRequested(library.itemId || "", library.name || "")
        if (hadFocus)
            Qt.callLater(restoreMainContentFocus)
    }

    /// Ordered list of focusable rail entries (top to bottom).
    function focusOrder() {
        var entries = []
        for (var i = 0; i < navRepeater.count; ++i) {
            var navEntry = navRepeater.itemAt(i)
            if (navEntry && navEntry.visible)
                entries.push(navEntry)
        }
        for (var j = 0; j < libraryRepeater.count; ++j) {
            var libraryEntry = libraryRepeater.itemAt(j)
            if (libraryEntry && libraryEntry.visible)
                entries.push(libraryEntry)
        }
        if (updatesEntry.visible)
            entries.push(updatesEntry)
        entries.push(settingsEntry)
        entries.push(accountEntry)
        entries.push(powerEntry)
        return entries
    }

    function moveFocus(fromItem, delta) {
        var entries = focusOrder()
        var index = entries.indexOf(fromItem)
        if (index < 0)
            return
        var next = Math.max(0, Math.min(entries.length - 1, index + delta))
        if (next !== index)
            entries[next].forceActiveFocus()
    }

    /// Focus the entry for the current page (Home, Search, a library, or
    /// Settings). Returns false when no matching entry is focusable.
    function focusActiveItem() {
        if (currentNavigation === "settings") {
            settingsEntry.forceActiveFocus()
            return true
        }
        if (currentNavigation === "updates" && updatesEntry.visible) {
            updatesEntry.forceActiveFocus()
            return true
        }
        if (currentLibraryId !== "") {
            for (var i = 0; i < libraryItems.length; ++i) {
                if (libraryItems[i] && libraryItems[i].itemId === currentLibraryId) {
                    var libraryEntry = libraryRepeater.itemAt(i)
                    if (libraryEntry) {
                        libraryEntry.forceActiveFocus()
                        return true
                    }
                    break
                }
            }
        }
        for (var j = 0; j < navigationItems.length; ++j) {
            if (navigationItems[j].id === currentNavigation) {
                var navEntry = navRepeater.itemAt(j)
                if (navEntry) {
                    navEntry.forceActiveFocus()
                    return true
                }
                break
            }
        }
        return false
    }

    /// Focus the rail (called from main content via Left arrow at edge). Lands
    /// on the current page's entry so a single Up/Down reaches neighbours.
    function focusRail() {
        if (!focusActiveItem()) {
            var entries = focusOrder()
            if (entries.length > 0)
                entries[0].forceActiveFocus()
        }
    }

    /// Rail entry that opened the current confirmation dialog (sign out,
    /// exit), so cancelling the dialog returns focus to it.
    property Item dialogOriginEntry: null

    function focusDialogOrigin() {
        var origin = dialogOriginEntry
        dialogOriginEntry = null
        if (origin && origin.visible)
            origin.forceActiveFocus()
        else
            focusRail()
    }

    /// Kept for callers written against the push-mode sidebar.
    function focusNavigation() {
        focusRail()
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
        SidebarSettings.libraryOrder = reordered.map(function (v) { return v.itemId })
        Qt.callLater(function() {
            var moved = libraryRepeater.itemAt(toIndex)
            if (moved)
                moved.forceActiveFocus()
        })
    }

    function finishReorder() {
        reorderingIndex = -1
    }

    function libraryIcon(library) {
        var collectionType = String((library && library.collectionType) || "").toLowerCase()
        if (collectionType === "tvshows") return Icons.tvShows
        if (collectionType === "movies") return Icons.movie
        if (collectionType === "music") return Icons.music
        if (collectionType === "musicvideos") return Icons.videocam
        if (collectionType === "boxsets") return Icons.videoLibrary
        if (collectionType === "homevideos" || collectionType === "photos") return Icons.photo
        return Icons.folder
    }

    readonly property string accountName: (typeof ConfigManager !== "undefined" && ConfigManager.username)
                                          ? ConfigManager.username : ""

    // ========================================
    // Rail entry
    // ========================================

    component RailEntry: AbstractButton {
        id: entry

        property string iconGlyph: ""
        property string label: ""
        property string sublabel: ""
        property string toolTipLabel: label
        property bool selected: false
        property bool showBadge: false
        property bool reordering: false
        property bool avatar: false
        property bool subdued: false
        property int libraryIndex: -1

        signal activated(bool withShift)

        width: railColumn.width
        height: Theme.navRailItemHeight
        // Pointer clicks navigate without moving keyboard focus into the rail.
        focusPolicy: Qt.NoFocus
        hoverEnabled: true

        Accessible.role: Accessible.MenuItem
        Accessible.name: sublabel !== "" ? label + ", " + sublabel : label
        Accessible.description: toolTipLabel
        Accessible.focusable: true

        onClicked: activated(false)

        onActiveFocusChanged: {
            if (activeFocus)
                root.ensureEntryVisible(entry)
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                if (!event.isAutoRepeat) {
                    root.activateKeyboardNavigation()
                    entry.activated((event.modifiers & Qt.ShiftModifier) && event.key !== Qt.Key_Space)
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                root.activateKeyboardNavigation()
                var delta = event.key === Qt.Key_Up ? -1 : 1
                if (root.reorderModeActive) {
                    // While reordering, Up/Down move the library being reordered.
                    if (entry.reordering) {
                        root.reorderLibrary(entry.libraryIndex,
                                            Math.max(0, Math.min(root.libraryItems.length - 1, entry.libraryIndex + delta)))
                    }
                } else {
                    root.moveFocus(entry, delta)
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                root.activateKeyboardNavigation()
                root.finishReorder()
                root.restoreMainContentFocus()
                event.accepted = true
            } else if (event.key === Qt.Key_Left) {
                event.accepted = true
            } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                root.activateKeyboardNavigation()
                if (root.reorderModeActive)
                    root.finishReorder()
                else
                    root.close()
                event.accepted = true
            }
        }

        ToolTip.visible: !root.expanded && hovered && toolTipLabel !== ""
        ToolTip.text: toolTipLabel
        ToolTip.delay: 400

        background: Rectangle {
            id: entryBackground
            radius: Theme.radiusMedium
            color: {
                if (entry.reordering) return Theme.surfaceInteractivePressed
                if (entry.down) return Theme.surfaceInteractivePressed
                if (entry.activeFocus) return Theme.surfaceInteractiveHover
                if (entry.selected) return Theme.selectionBackground
                if (entry.hovered) return Theme.surfaceInteractive
                return "transparent"
            }
            Behavior on color {
                enabled: Theme.uiAnimationsEnabled
                ColorAnimation { duration: Theme.durationShort }
            }

            // Current-page indicator: an accent bar on the leading edge.
            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: Math.round(6 * Theme.layoutScale)
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.selectionIndicatorWidth
                height: entry.selected ? Math.round(parent.height * 0.44) : 0
                radius: width / 2
                color: Theme.selectionIndicator
                visible: height > 0
                Behavior on height {
                    enabled: Theme.uiAnimationsEnabled
                    NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic }
                }
            }

            FocusRing {
                inset: true
                active: entry.activeFocus || entry.reordering
                color: entry.reordering ? Theme.selectionIndicator : Theme.focusRing
                radius: entryBackground.radius
            }
        }

        contentItem: Item {
            Item {
                id: iconSlot
                x: 0
                width: root.collapsedWidth - root.railPadding * 2
                height: parent.height

                Text {
                    anchors.centerIn: parent
                    visible: !entry.avatar
                    text: entry.iconGlyph
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.iconSizeNav
                    color: entry.selected ? Theme.selectionIndicator
                         : (entry.subdued && !entry.activeFocus ? Theme.textSecondary : Theme.textPrimary)
                    Behavior on color {
                        enabled: Theme.uiAnimationsEnabled
                        ColorAnimation { duration: Theme.durationShort }
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    visible: entry.avatar
                    width: Math.round(42 * Theme.layoutScale)
                    height: width
                    radius: width / 2
                    color: Theme.selectionBackground
                    border.width: Theme.borderWidth
                    border.color: Theme.surfaceBorderStrong

                    Text {
                        anchors.centerIn: parent
                        text: entry.iconGlyph
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: Theme.fontWeightStrong
                        color: Theme.textPrimary
                    }
                }

                Rectangle {
                    visible: entry.showBadge
                    width: Math.round(12 * Theme.layoutScale)
                    height: width
                    radius: width / 2
                    x: parent.width / 2 + Math.round(10 * Theme.layoutScale)
                    y: parent.height / 2 - Math.round(20 * Theme.layoutScale)
                    color: Theme.accentPrimary
                    border.width: Math.max(1, Math.round(2 * Theme.layoutScale))
                    border.color: Theme.backgroundPrimary
                }
            }

            Column {
                anchors.left: iconSlot.right
                anchors.leftMargin: Math.round(6 * Theme.layoutScale)
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingMedium
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(2 * Theme.layoutScale)
                opacity: root.expanded ? 1.0 : 0.0
                visible: opacity > 0
                Behavior on opacity {
                    enabled: Theme.uiAnimationsEnabled
                    NumberAnimation { duration: root.animDuration }
                }

                Text {
                    width: parent.width
                    text: entry.label
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    font.weight: entry.selected || entry.activeFocus ? Theme.fontWeightStrong : Theme.fontWeightMedium
                    color: entry.selected || entry.activeFocus ? Theme.textPrimary
                         : (entry.subdued ? Theme.textSecondary : Theme.textPrimary)
                }

                Text {
                    width: parent.width
                    visible: text !== ""
                    text: entry.sublabel
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }
            }
        }
    }

    function ensureEntryVisible(entry) {
        if (!entry || !libraryFlick.contentItem)
            return
        if (entry.parent !== libraryColumn)
            return
        var top = entry.y
        var bottom = entry.y + entry.height
        if (top < libraryFlick.contentY)
            libraryFlick.contentY = top
        else if (bottom > libraryFlick.contentY + libraryFlick.height)
            libraryFlick.contentY = bottom - libraryFlick.height
    }

    // ========================================
    // Pointer dismissal + legibility scrim (expanded only)
    // ========================================

    Rectangle {
        id: expandedScrim
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: root.expandedWidth + Theme.navRailInset + Math.round(260 * Theme.layoutScale)
        opacity: root.expanded ? 1.0 : 0.0
        visible: opacity > 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.78) }
            GradientStop { position: 0.55; color: Theme.withAlpha(Theme.backgroundPrimary, 0.55) }
            GradientStop { position: 1.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
        }
        Behavior on opacity {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: root.animDuration }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.pinnedOpen
        visible: enabled
        onClicked: root.close()
    }

    // ========================================
    // Rail panel
    // ========================================

    Rectangle {
        id: railPanel
        objectName: "railPanel"
        x: Theme.navRailInset
        y: Theme.navRailInset
        width: root.expanded ? root.expandedWidth : root.collapsedWidth
        height: parent.height - Theme.navRailInset * 2
        radius: Theme.radiusPanel
        color: root.expanded ? Theme.navRailBackground : Theme.navRailBackgroundCollapsed
        border.width: Theme.borderWidth
        border.color: Theme.surfaceBorder
        antialiasing: true

        Behavior on width {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: root.animDuration; easing.type: Theme.easingStandard }
        }
        Behavior on color {
            enabled: Theme.uiAnimationsEnabled
            ColorAnimation { duration: root.animDuration }
        }

        // Swallow pointer events so clicks on the panel never reach the page.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function(wheel) { wheel.accepted = false }
        }

        ColumnLayout {
            id: railColumn
            anchors.fill: parent
            anchors.margins: root.railPadding
            spacing: Math.round(4 * Theme.layoutScale)

            // Brand + pointer toggle. Keyboard users expand the rail by moving
            // focus into it, so the toggle is pointer-only.
            AbstractButton {
                id: railToggle
                objectName: "railToggle"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(84 * Theme.layoutScale)
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                onClicked: root.pinnedOpen = !root.expanded

                Accessible.role: Accessible.Button
                Accessible.name: root.expanded ? qsTr("Collapse navigation") : qsTr("Expand navigation")

                ToolTip.visible: hovered && !root.expanded
                ToolTip.text: qsTr("Show labels")
                ToolTip.delay: 400

                background: Rectangle {
                    radius: Theme.radiusMedium
                    color: railToggle.hovered ? Theme.surfaceInteractive : "transparent"
                }

                contentItem: Item {
                    Image {
                        id: railLogo
                        x: Math.round((root.collapsedWidth - root.railPadding * 2 - width) / 2)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.round(46 * Theme.layoutScale)
                        height: width
                        source: "qrc:/images/app/logo_trans.svg"
                        sourceSize.width: width * 2
                        sourceSize.height: height * 2
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }

                    Text {
                        anchors.left: railLogo.right
                        anchors.leftMargin: Math.round(20 * Theme.layoutScale)
                        anchors.verticalCenter: parent.verticalCenter
                        text: "bloom"
                        font.pixelSize: Theme.fontSizeTitle
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightStrong
                        font.letterSpacing: Math.round(0.5 * Theme.layoutScale)
                        color: Theme.accentPrimary
                        opacity: root.expanded ? 1.0 : 0.0
                        visible: opacity > 0
                        Behavior on opacity {
                            enabled: Theme.uiAnimationsEnabled
                            NumberAnimation { duration: root.animDuration }
                        }
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingSmall
                        anchors.verticalCenter: parent.verticalCenter
                        text: Icons.chevronLeft
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeIcon
                        color: Theme.textSecondary
                        visible: root.pinnedOpen
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.spacingSmall }

            Repeater {
                id: navRepeater
                model: root.navigationItems

                RailEntry {
                    required property var modelData
                    objectName: "railEntry_" + modelData.id
                    iconGlyph: modelData.icon
                    label: modelData.label
                    toolTipLabel: modelData.tooltip
                    selected: root.currentNavigation === modelData.id
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.navRailItemHeight
                    onActivated: root.handleNavigation(modelData.id)
                }
            }

            // Library section
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: libraryHeader.visible ? libraryHeader.implicitHeight + Theme.spacingSmall * 2
                                                              : Theme.spacingSmall * 2

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    height: Theme.borderWidth
                    color: Theme.surfaceBorder
                    visible: !libraryHeader.visible && libraryRepeater.count > 0
                }

                Text {
                    id: libraryHeader
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("My Media")
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightStrong
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: Math.round(1.5 * Theme.layoutScale)
                    color: Theme.textTertiary
                    visible: root.expanded && libraryRepeater.count > 0
                }
            }

            Flickable {
                id: libraryFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: Theme.navRailItemHeight
                contentWidth: width
                contentHeight: libraryColumn.implicitHeight
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Behavior on contentY {
                    enabled: Theme.uiAnimationsEnabled
                    NumberAnimation { duration: Theme.durationShort; easing.type: Theme.easingStandard }
                }

                Column {
                    id: libraryColumn
                    width: libraryFlick.width
                    spacing: Math.round(4 * Theme.layoutScale)

                    Repeater {
                        id: libraryRepeater
                        model: root.libraryItems

                        RailEntry {
                            id: libraryEntry
                            objectName: "railLibrary_" + index
                            required property var modelData
                            required property int index
                            width: libraryColumn.width
                            iconGlyph: root.libraryIcon(modelData)
                            label: modelData.name || ""
                            sublabel: reordering ? qsTr("Up/Down to move, Enter to finish") : ""
                            toolTipLabel: modelData.name || ""
                            selected: root.currentNavigation === ("library:" + (modelData.itemId || ""))
                            reordering: root.reorderModeActive && root.reorderingIndex === index
                            libraryIndex: index
                            Accessible.description: qsTr("Open library %1. Shift+Enter to reorder.").arg(modelData.name || "")

                            onActivated: function(withShift) {
                                if (withShift) {
                                    if (reordering)
                                        root.finishReorder()
                                    else
                                        root.reorderingIndex = index
                                    return
                                }
                                if (root.reorderModeActive) {
                                    root.finishReorder()
                                    return
                                }
                                root.activateLibrary(index)
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingSmall
                Layout.rightMargin: Theme.spacingSmall
                Layout.topMargin: Theme.spacingSmall
                Layout.bottomMargin: Theme.spacingSmall
                Layout.preferredHeight: Theme.borderWidth
                color: Theme.surfaceBorder
            }

            RailEntry {
                id: updatesEntry
                objectName: "railEntry_updates"
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Theme.navRailItemHeight : 0
                visible: root._updatesAvailable
                iconGlyph: Icons.download
                label: qsTr("Update available")
                toolTipLabel: qsTr("Update available")
                showBadge: true
                selected: root.currentNavigation === "updates"
                onActivated: root.handleNavigation("updates")
            }

            RailEntry {
                id: settingsEntry
                objectName: "railEntry_settings"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.navRailItemHeight
                iconGlyph: Icons.settings
                label: qsTr("Settings")
                selected: root.currentNavigation === "settings"
                onActivated: root.handleNavigation("settings")
            }

            RailEntry {
                id: accountEntry
                objectName: "railEntry_account"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.navRailItemHeight
                avatar: root.accountName !== ""
                iconGlyph: root.accountName !== "" ? root.accountName.charAt(0).toUpperCase() : Icons.logout
                label: root.accountName !== "" ? root.accountName : qsTr("Sign Out")
                sublabel: root.accountName !== "" ? qsTr("Sign out") : ""
                toolTipLabel: root.accountName !== "" ? qsTr("Sign out %1").arg(root.accountName) : qsTr("Sign out")
                onActivated: {
                    root.pinnedOpen = false
                    root.dialogOriginEntry = accountEntry
                    root.signOutRequested()
                }
            }

            RailEntry {
                id: powerEntry
                objectName: "railEntry_exit"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.navRailItemHeight
                iconGlyph: Icons.power
                label: qsTr("Exit")
                toolTipLabel: qsTr("Exit")
                subdued: true
                onActivated: {
                    root.pinnedOpen = false
                    root.dialogOriginEntry = powerEntry
                    root.exitRequested()
                }
            }
        }
    }

    // ESC closes a pointer-pinned rail even when focus is in the page.
    Shortcut {
        sequence: "Escape"
        enabled: root.pinnedOpen && !root.activeFocus
        onActivated: root.close()
    }

    onActiveFocusChanged: {
        if (!activeFocus)
            finishReorder()
    }

    // ========================================
    // Accessibility
    // ========================================

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Navigation")
    Accessible.description: expanded ? qsTr("Navigation is expanded") : qsTr("Navigation is collapsed")
}
