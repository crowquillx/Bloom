import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * NavigationBar - Bloom's floating top navigation.
 *
 * - Floats over the top of every page on its own soft scrim: brand on the
 *   left, page tabs (Home, Movies, Series, Search), and a utility cluster
 *   (update, Settings, space/account, power) on the right. Hero pages draw
 *   under it; other pages start below Theme.navBarReservedHeight.
 * - Movies and Series stand for every library of that kind. With one library
 *   the tab opens it; with several it drops down a small menu: "All Movies"
 *   (the libraries combined) and then each library. Merged spaces group the
 *   menu by source.
 * - The current page shows the selection fill; the focused entry shows the
 *   shared FocusRing. Selection and focus stay distinct.
 * - Pages recede the bar (`recessed`) once the user scrolls into content. It
 *   returns whenever focus or the pointer comes back to it.
 * - Left/Right walk every entry in order, Down/Escape return focus to the
 *   page, Enter/Space activate. In the menu Up/Down move, Enter opens,
 *   Escape/Left/Right close it, and Shift+Enter starts reordering a library
 *   (Up/Down then move it, Enter finishes).
 * - Pointer clicks navigate directly without stealing keyboard focus.
 */
FocusScope {
    id: root

    // ========================================
    // Public API
    // ========================================

    /// The page asked the bar to get out of the way (scrolled into content).
    property bool recessed: false

    /// Whether keyboard focus is in the bar. Kept under the old rail name so
    /// shell code can gate shortcuts on "navigation is open".
    readonly property bool expanded: root.activeFocus

    /// Bar is on screen: not recessed, or focused, or pointer-revealed.
    readonly property bool shown: !recessed || root.activeFocus || pointerRevealed
    /// The pointer (not a stale cursor position) is resting at the top edge.
    readonly property bool pointerRevealed: pointerReveal.containsMouse
                                            && typeof InputModeManager !== "undefined"
                                            && InputModeManager.pointerActive === true

    /// Navigation destinations around the library list.
    readonly property var leadingItems: [
        { id: "home", icon: Icons.home, label: qsTr("Home") }
    ]
    readonly property var trailingItems: [
        { id: "search", icon: Icons.search, label: qsTr("Search") }
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

    /// Single spaces filter here; merged views arrive already filtered.
    function visibleLibraries(views) {
        return typeof Workspaces !== "undefined" ? Workspaces.visibleLibraries(views) : views
    }

    /// Saved order after moving visible libraries; hidden ones keep their slots.
    function savedOrderWith(visibleIds) {
        var isVisible = {}
        for (var i = 0; i < visibleIds.length; i++)
            isVisible[visibleIds[i]] = true
        var saved = SidebarSettings.libraryOrder || []
        var order = []
        var next = 0
        for (var j = 0; j < saved.length; j++)
            order.push(isVisible[saved[j]] && next < visibleIds.length ? visibleIds[next++] : saved[j])
        while (next < visibleIds.length)
            order.push(visibleIds[next++])
        return order
    }

    /// Merged spaces list every source's libraries in member-priority
    /// order; the per-server saved order and reordering do not apply there.
    readonly property bool mergedLibraries: typeof MergedCatalog !== "undefined" && MergedCatalog.active

    function syncLibraryItems() {
        if (mergedLibraries) {
            orderedLibraryItems = MergedCatalog.views || []
            libraryItems = orderedLibraryItems
            finishReorder()
            return
        }
        var views = LibraryViewModel.views || []
        // Order first: the saved order keeps hidden libraries in place.
        orderedLibraryItems = visibleLibraries(computeOrderedLibraries(views))
        libraryItems = orderedLibraryItems
    }

    /// Current active navigation item
    property string currentNavigation: "home"
    /// Current active library id (derived from currentNavigation when applicable)
    property string currentLibraryId: ""
    /// Connection that owns the open library; merged spaces list the same
    /// library ID once per account on a shared server.
    property string currentLibraryConnectionId: ""

    /// "movies" or "series" (views arrive tagged by LibraryKinds).
    function libraryKindOf(library) {
        if (!library)
            return ""
        if (library.libraryKind)
            return library.libraryKind
        var type = String(library.collectionType || "").toLowerCase()
        if (type === "movies")
            return "movies"
        if (type === "tvshows" || type === "series")
            return "series"
        return ""
    }

    function librariesOfKind(kind) {
        return libraryItems.filter(function(library) { return libraryKindOf(library) === kind })
    }

    /// The combined page ("All Movies") for @p kind is open.
    function isCurrentCombined(kind, connectionId) {
        if (currentNavigation !== "library:combined:" + kind)
            return false
        return !connectionId || currentLibraryConnectionId === ""
                || currentLibraryConnectionId === connectionId
    }

    /// The open page belongs to @p kind: one of its libraries or its combined page.
    function isCurrentKind(kind) {
        if (currentNavigation === "library:combined:" + kind)
            return true
        var libraries = librariesOfKind(kind)
        for (var i = 0; i < libraries.length; ++i) {
            if (isCurrentLibrary(libraries[i]))
                return true
        }
        return false
    }

    readonly property var kindTabs: {
        var tabs = []
        if (librariesOfKind("movies").length > 0)
            tabs.push({ id: "movies", icon: Icons.movie, label: qsTr("Movies") })
        if (librariesOfKind("series").length > 0)
            tabs.push({ id: "series", icon: Icons.tvShows, label: qsTr("Series") })
        return tabs
    }

    function isCurrentLibrary(library) {
        if (!library || currentNavigation !== ("library:" + (library.itemId || "")))
            return false
        return !library.connectionId || currentLibraryConnectionId === ""
                || library.connectionId === currentLibraryConnectionId
    }

    // Library ordering state
    property var libraryItems: []

    property bool _updatesReady: false
    property bool _updatesAvailable: false

    /// Signal emitted when a navigation item is selected
    signal navigationRequested(string navigationId)
    /// Signal emitted when a library is selected
    signal libraryRequested(string libraryId, string libraryName, string collectionType)
    /// Signal emitted when every library of a kind is selected ("All Movies")
    signal combinedLibraryRequested(string kind, var libraryIds, string title)
    /// Signal emitted when user requests to sign out
    signal signOutRequested()
    /// Signal emitted when the account entry opens the space picker
    signal spacePickerRequested()
    /// Signal emitted when user requests to exit the app
    signal exitRequested()

    /// Reference to the main content for focus return
    property Item mainContent: null

    readonly property int animDuration: (SidebarSettings.reduceMotion || !Theme.uiAnimationsEnabled)
                                        ? 0 : Theme.durationLong

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

    Connections {
        target: typeof MergedCatalog !== "undefined" ? MergedCatalog : null
        ignoreUnknownSignals: true
        function onViewsChanged() { syncLibraryItems() }
        function onActiveChanged() { syncLibraryItems() }
    }

    Connections {
        target: typeof Workspaces !== "undefined" ? Workspaces : null
        ignoreUnknownSignals: true
        function onLibraryVisibilityChanged() { syncLibraryItems() }
        function onActiveProfileChanged() { syncLibraryItems() }
    }

    SourceRouter {
        id: libraryRouter
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
        if (root.activeFocus) {
            close()
        } else {
            activateKeyboardNavigation()
            focusBar()
        }
    }

    function open() {
        focusBar()
    }

    function activateKeyboardNavigation() {
        if (typeof InputModeManager === "undefined")
            return
        InputModeManager.setNavigationMode("keyboard")
        InputModeManager.hideCursor(true)
    }

    function close() {
        var hadFocus = root.activeFocus
        finishReorder()
        if (!hadFocus)
            return
        Qt.callLater(restoreMainContentFocus)
    }

    function restoreMainContentFocus() {
        if (!mainContent)
            return
        if (typeof mainContent.restoreFocusFromNavigation === "function") {
            mainContent.restoreFocusFromNavigation()
        } else {
            mainContent.forceActiveFocus()
        }
    }

    function handleNavigation(navigationId) {
        var hadFocus = root.activeFocus
        currentNavigation = navigationId
        if (navigationId.indexOf("library:") !== 0) {
            currentLibraryId = ""
            currentLibraryConnectionId = ""
        }
        navigationRequested(navigationId)
        if (hadFocus)
            Qt.callLater(restoreMainContentFocus)
    }

    function activateLibrary(library) {
        if (!library)
            return
        var hadFocus = root.activeFocus
        closeMenu()
        currentNavigation = "library:" + (library.itemId || "")
        currentLibraryId = library.itemId || ""
        currentLibraryConnectionId = library.connectionId || ""
        // A merged library opens on the connection that owns it.
        libraryRouter.run(library.connectionId || "", function() {
            root.libraryRequested(library.itemId || "", library.name || "",
                                  library.collectionType || "")
        })
        if (hadFocus)
            Qt.callLater(restoreMainContentFocus)
    }

    function combinedTitle(kind, sourceLabel) {
        var title = kind === "series" ? qsTr("All Series") : qsTr("All Movies")
        return sourceLabel ? qsTr("%1 · %2").arg(title).arg(sourceLabel) : title
    }

    /// Opens every library of @p kind on one connection as one page.
    function activateCombined(kind, connectionId, sourceLabel, libraryIds) {
        var hadFocus = root.activeFocus
        closeMenu()
        currentNavigation = "library:combined:" + kind
        currentLibraryId = "combined:" + kind
        currentLibraryConnectionId = connectionId || ""
        libraryRouter.run(connectionId || "", function() {
            root.combinedLibraryRequested(kind, libraryIds, root.combinedTitle(kind, sourceLabel))
        })
        if (hadFocus)
            Qt.callLater(restoreMainContentFocus)
    }

    /// Movies/Series tab: one library opens directly, several open the menu.
    function activateKind(kind, tab) {
        var libraries = librariesOfKind(kind)
        if (libraries.length === 1) {
            activateLibrary(libraries[0])
            return
        }
        if (libraries.length > 1)
            openMenu(kind, tab)
    }

    // ---- Library menu ----

    property string menuKind: ""
    property Item menuAnchor: null
    readonly property bool menuOpen: menuKind !== ""
    /// Library ID being reordered from the menu ("" when not reordering).
    property string reorderingLibraryId: ""
    readonly property bool reorderModeActive: reorderingLibraryId !== ""

    /// Menu rows for @p kind: per source (in library order), "All …" when the
    /// source has more than one library, then each library. Merged spaces
    /// with several sources add a source header above each group.
    function menuEntries(kind, libraries) {
        var groups = []
        var byConnection = {}
        for (var i = 0; i < libraries.length; ++i) {
            var library = libraries[i]
            if (libraryKindOf(library) !== kind)
                continue
            var key = library.connectionId || ""
            if (byConnection[key] === undefined) {
                byConnection[key] = groups.length
                groups.push({ connectionId: key, sourceLabel: library.sourceLabel || "", libraries: [] })
            }
            groups[byConnection[key]].libraries.push(library)
        }
        var entries = []
        var labelled = groups.length > 1
        for (var g = 0; g < groups.length; ++g) {
            var group = groups[g]
            if (labelled && group.sourceLabel !== "")
                entries.push({ type: "header", label: group.sourceLabel })
            if (group.libraries.length > 1) {
                entries.push({
                    type: "all", kind: kind, connectionId: group.connectionId,
                    sourceLabel: labelled ? group.sourceLabel : "",
                    label: kind === "series" ? qsTr("All Series") : qsTr("All Movies"),
                    libraryIds: group.libraries.map(function(l) { return l.itemId })
                })
            }
            for (var j = 0; j < group.libraries.length; ++j)
                entries.push({ type: "library", library: group.libraries[j], label: group.libraries[j].name || "" })
        }
        return entries
    }

    readonly property var currentMenuEntries: menuOpen ? menuEntries(menuKind, libraryItems) : []

    function openMenu(kind, tab) {
        finishReorder()
        menuAnchor = tab
        menuKind = kind
        Qt.callLater(focusMenuCurrent)
    }

    function closeMenu() {
        finishReorder()
        var anchor = menuAnchor
        var hadFocus = menuPanel.activeFocus
        menuKind = ""
        menuAnchor = null
        if (hadFocus && anchor)
            anchor.forceActiveFocus()
    }

    /// Closes the menu and moves along the bar from its tab (Left/Right).
    function closeMenuAndMove(delta) {
        var anchor = menuAnchor
        closeMenu()
        if (anchor)
            moveFocus(anchor, delta)
    }

    function menuRows() {
        var rows = []
        for (var i = 0; i < menuRepeater.count; ++i) {
            var row = menuRepeater.itemAt(i)
            if (row && row.focusable)
                rows.push(row)
        }
        return rows
    }

    /// Focus the menu row for the open page, or the first row.
    function focusMenuCurrent() {
        var rows = menuRows()
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].selected) {
                rows[i].forceActiveFocus()
                return
            }
        }
        if (rows.length > 0)
            rows[0].forceActiveFocus()
    }

    function moveMenuFocus(fromRow, delta) {
        var rows = menuRows()
        var index = rows.indexOf(fromRow)
        var next = Math.max(0, Math.min(rows.length - 1, index + delta))
        if (index >= 0 && next !== index)
            rows[next].forceActiveFocus()
    }

    function focusMenuLibrary(libraryId) {
        var rows = menuRows()
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].entry.type === "library" && rows[i].entry.library.itemId === libraryId) {
                rows[i].forceActiveFocus()
                return
            }
        }
    }

    /// Ordered list of focusable entries (left to right).
    function focusOrder() {
        var entries = []
        var i
        for (i = 0; i < leadingRepeater.count; ++i) {
            var lead = leadingRepeater.itemAt(i)
            if (lead && lead.visible)
                entries.push(lead)
        }
        for (i = 0; i < kindRepeater.count; ++i) {
            var kindEntry = kindRepeater.itemAt(i)
            if (kindEntry && kindEntry.visible)
                entries.push(kindEntry)
        }
        for (i = 0; i < trailingRepeater.count; ++i) {
            var trail = trailingRepeater.itemAt(i)
            if (trail && trail.visible)
                entries.push(trail)
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
            for (var i = 0; i < kindTabs.length; ++i) {
                if (isCurrentKind(kindTabs[i].id)) {
                    var kindEntry = kindRepeater.itemAt(i)
                    if (kindEntry) {
                        kindEntry.forceActiveFocus()
                        return true
                    }
                    break
                }
            }
        }
        var repeaters = [leadingRepeater, trailingRepeater]
        var models = [leadingItems, trailingItems]
        for (var r = 0; r < repeaters.length; ++r) {
            for (var j = 0; j < models[r].length; ++j) {
                if (models[r][j].id === currentNavigation) {
                    var navEntry = repeaters[r].itemAt(j)
                    if (navEntry) {
                        navEntry.forceActiveFocus()
                        return true
                    }
                }
            }
        }
        return false
    }

    /// Focus the bar (Up from the top of a page). Lands on the current page's
    /// entry so a single Left/Right reaches its neighbours.
    function focusBar() {
        if (!focusActiveItem()) {
            var entries = focusOrder()
            if (entries.length > 0)
                entries[0].forceActiveFocus()
        }
    }

    /// Entry that opened the current confirmation dialog (sign out, exit), so
    /// cancelling the dialog returns focus to it.
    property Item dialogOriginEntry: null

    function focusDialogOrigin() {
        var origin = dialogOriginEntry
        dialogOriginEntry = null
        if (origin && origin.visible)
            origin.forceActiveFocus()
        else
            focusBar()
    }

    /// Alias used by shell code that asks for "the navigation".
    function focusNavigation() {
        focusBar()
    }

    /// Moves @p libraryId past its neighbour of the same kind (delta -1/+1).
    function reorderLibrary(libraryId, delta) {
        var fromIndex = -1
        for (var i = 0; i < libraryItems.length; ++i) {
            if (libraryItems[i].itemId === libraryId) {
                fromIndex = i
                break
            }
        }
        if (fromIndex < 0)
            return
        var kind = libraryKindOf(libraryItems[fromIndex])
        var toIndex = -1
        for (var j = fromIndex + delta; j >= 0 && j < libraryItems.length; j += delta) {
            if (libraryKindOf(libraryItems[j]) === kind) {
                toIndex = j
                break
            }
        }
        if (toIndex < 0)
            return
        var reordered = libraryItems.slice()
        var item = reordered.splice(fromIndex, 1)[0]
        reordered.splice(toIndex, 0, item)
        libraryItems = reordered
        orderedLibraryItems = reordered
        SidebarSettings.libraryOrder = savedOrderWith(reordered.map(function (v) { return v.itemId }))
        Qt.callLater(function() { root.focusMenuLibrary(libraryId) })
    }

    function finishReorder() {
        reorderingLibraryId = ""
    }

    function libraryIcon(library) {
        return libraryKindOf(library) === "series" ? Icons.tvShows : Icons.movie
    }

    readonly property string accountName: (typeof ConfigManager !== "undefined" && ConfigManager.username)
                                          ? ConfigManager.username : ""
    readonly property string spaceName: typeof Workspaces !== "undefined" ? Workspaces.activeProfileName : ""

    // Keep the focused tab inside the scrollable strip.
    function ensureTabVisible(entry) {
        if (!entry || entry.parent !== tabRow)
            return
        var left = entry.x
        var right = entry.x + entry.width
        var pad = Theme.spacingLarge
        if (left - pad < tabStrip.contentX)
            tabStrip.contentX = Math.max(0, left - pad)
        else if (right + pad > tabStrip.contentX + tabStrip.width)
            tabStrip.contentX = Math.min(tabStrip.contentWidth - tabStrip.width, right + pad - tabStrip.width)
    }

    // ========================================
    // Entry
    // ========================================

    // `kind` is "tab" (icon + label pill), "icon" (round utility button) or
    // "avatar" (space monogram).
    component NavEntry: AbstractButton {
        id: entry

        property string kind: "tab"
        property string iconGlyph: ""
        property string label: ""
        property string toolTipLabel: label
        property bool selected: false
        property bool showBadge: false
        /// Opens the library menu: shows a small chevron.
        property bool hasMenu: false

        signal activated(bool withShift)

        implicitHeight: Theme.navBarItemHeight
        implicitWidth: kind === "tab" ? tabContent.implicitWidth + Math.round(52 * Theme.layoutScale)
                                      : implicitHeight
        // Pointer clicks navigate without moving keyboard focus into the bar.
        focusPolicy: Qt.NoFocus
        hoverEnabled: true

        Accessible.role: Accessible.MenuItem
        Accessible.name: label
        Accessible.description: toolTipLabel
        Accessible.focusable: true

        onClicked: activated(false)

        onActiveFocusChanged: {
            if (activeFocus)
                root.ensureTabVisible(entry)
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                if (!event.isAutoRepeat) {
                    root.activateKeyboardNavigation()
                    entry.activated((event.modifiers & Qt.ShiftModifier) && event.key !== Qt.Key_Space)
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                root.activateKeyboardNavigation()
                root.moveFocus(entry, event.key === Qt.Key_Left ? -1 : 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                root.activateKeyboardNavigation()
                root.restoreMainContentFocus()
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                event.accepted = true
            } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                root.activateKeyboardNavigation()
                root.close()
                event.accepted = true
            }
        }

        ToolTip.visible: hovered && entry.kind !== "tab" && toolTipLabel !== ""
        ToolTip.text: toolTipLabel
        ToolTip.delay: 400

        background: Rectangle {
            id: entryBackground
            radius: height / 2
            color: {
                if (entry.down) return Theme.surfaceInteractivePressed
                if (entry.selected) return entry.activeFocus || entry.hovered
                                            ? Theme.mix(Theme.selectionBackground, Theme.surfaceInteractiveHover, 0.5)
                                            : Theme.selectionBackground
                if (entry.activeFocus) return Theme.surfaceInteractiveHover
                if (entry.hovered) return Theme.surfaceInteractive
                return entry.kind === "icon" ? Theme.withAlpha(Theme.surfaceInteractive, 0.6) : "transparent"
            }
            border.width: entry.selected ? Theme.borderWidth : 0
            border.color: Theme.withAlpha(Theme.selectionIndicator, 0.55)
            antialiasing: true
            Behavior on color {
                enabled: Theme.uiAnimationsEnabled
                ColorAnimation { duration: Theme.durationShort }
            }

            FocusRing {
                active: entry.activeFocus
                radius: entryBackground.radius
            }
        }

        contentItem: Item {
            RowLayout {
                id: tabContent
                anchors.centerIn: parent
                spacing: Math.round(12 * Theme.layoutScale)
                visible: entry.kind === "tab"

                Text {
                    text: entry.iconGlyph
                    visible: text !== ""
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.iconSizeLarge
                    color: entry.selected ? Theme.selectionIndicator : (entry.activeFocus || entry.hovered ? Theme.textPrimary : Theme.textSecondary)
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: entry.label
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeMedium
                    font.weight: entry.selected || entry.activeFocus ? Theme.fontWeightStrong : Theme.fontWeightMedium
                    color: entry.selected || entry.activeFocus || entry.hovered ? Theme.textPrimary : Theme.textSecondary
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    visible: entry.hasMenu
                    text: Icons.expandMore
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.iconSizeMedium
                    color: Theme.textSecondary
                    rotation: root.menuOpen && root.menuAnchor === entry ? 180 : 0
                    Layout.leftMargin: -Math.round(6 * Theme.layoutScale)
                    Layout.alignment: Qt.AlignVCenter
                    Behavior on rotation {
                        enabled: Theme.uiAnimationsEnabled
                        NumberAnimation { duration: Theme.durationShort }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: entry.kind === "icon"
                text: entry.iconGlyph
                font.family: Theme.fontIcon
                font.pixelSize: Theme.iconSizeNav
                color: entry.selected ? Theme.selectionIndicator : Theme.textPrimary
            }

            Rectangle {
                anchors.centerIn: parent
                visible: entry.kind === "avatar"
                width: Math.round(parent.height * 0.82)
                height: width
                radius: width / 2
                color: Theme.mix(Theme.accentSecondary, Theme.accentPrimary, 0.5)
                border.width: Math.max(1, Math.round(2 * Theme.layoutScale))
                border.color: Theme.withAlpha(Theme.textPrimary, 0.55)

                Text {
                    anchors.centerIn: parent
                    text: entry.iconGlyph
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Theme.fontWeightHeavy
                    color: Theme.textOnAccent
                }
            }

            Rectangle {
                visible: entry.showBadge
                width: Math.round(14 * Theme.layoutScale)
                height: width
                radius: width / 2
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: Math.round(4 * Theme.layoutScale)
                anchors.topMargin: Math.round(4 * Theme.layoutScale)
                color: Theme.accentPrimary
                border.width: Math.max(1, Math.round(2 * Theme.layoutScale))
                border.color: Theme.backgroundPrimary
            }
        }
    }

    // A row in the library menu: a source header, "All Movies", or a library.
    component MenuRow: AbstractButton {
        id: row

        required property var modelData
        required property int index
        readonly property var entry: modelData
        readonly property bool focusable: entry.type !== "header"
        readonly property bool reordering: entry.type === "library"
                                           && root.reorderingLibraryId === entry.library.itemId
        readonly property bool selected: entry.type === "all"
                                         ? root.isCurrentCombined(entry.kind, entry.connectionId)
                                         : entry.type === "library" && root.isCurrentLibrary(entry.library)

        objectName: entry.type === "library" ? "navMenu_library_" + entry.library.itemId
                    : (entry.type === "all" ? "navMenu_all_" + (entry.connectionId || "") : "navMenu_header_" + index)
        width: parent ? parent.width : implicitWidth
        implicitHeight: focusable ? Theme.navBarItemHeight : Math.round(48 * Theme.layoutScale)
        focusPolicy: Qt.NoFocus
        hoverEnabled: focusable
        enabled: true

        Accessible.role: focusable ? Accessible.MenuItem : Accessible.StaticText
        Accessible.name: entry.type === "all" && entry.sourceLabel ? entry.label + ", " + entry.sourceLabel
                                                                  : entry.label
        Accessible.description: reordering ? qsTr("Up and Down move the library, Enter finishes.")
                                : entry.type === "library" && !root.mergedLibraries
                                  ? qsTr("Shift+Enter to reorder.") : ""
        Accessible.focusable: focusable

        function activate(withShift) {
            if (entry.type === "header")
                return
            if (entry.type === "library" && withShift && !root.mergedLibraries) {
                root.reorderingLibraryId = reordering ? "" : entry.library.itemId
                return
            }
            if (root.reorderModeActive) {
                root.finishReorder()
                return
            }
            if (entry.type === "all")
                root.activateCombined(entry.kind, entry.connectionId, entry.sourceLabel, entry.libraryIds)
            else
                root.activateLibrary(entry.library)
        }

        onClicked: activate(false)

        Keys.onPressed: function(event) {
            root.activateKeyboardNavigation()
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                // Accept first: activating closes the menu and destroys this row.
                event.accepted = true
                if (!event.isAutoRepeat)
                    row.activate((event.modifiers & Qt.ShiftModifier) && event.key !== Qt.Key_Space)
            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                var delta = event.key === Qt.Key_Up ? -1 : 1
                if (row.reordering)
                    root.reorderLibrary(row.entry.library.itemId, delta)
                else if (!root.reorderModeActive)
                    root.moveMenuFocus(row, delta)
                event.accepted = true
            } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                // Accept first: closing the menu destroys this row.
                event.accepted = true
                root.closeMenuAndMove(event.key === Qt.Key_Left ? -1 : 1)
            } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                event.accepted = true
                if (root.reorderModeActive)
                    root.finishReorder()
                else
                    root.closeMenu()
            }
        }

        background: Rectangle {
            id: rowBackground
            visible: row.focusable
            radius: Theme.radiusMedium
            color: {
                if (row.reordering || row.down) return Theme.surfaceInteractivePressed
                if (row.activeFocus) return Theme.surfaceInteractiveHover
                if (row.selected) return Theme.selectionBackground
                if (row.hovered) return Theme.surfaceInteractive
                return "transparent"
            }
            Behavior on color {
                enabled: Theme.uiAnimationsEnabled
                ColorAnimation { duration: Theme.durationShort }
            }

            // Current page: accent bar on the leading edge.
            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: Math.round(6 * Theme.layoutScale)
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.selectionIndicatorWidth
                height: row.selected ? Math.round(parent.height * 0.44) : 0
                radius: width / 2
                color: Theme.selectionIndicator
                visible: height > 0
            }

            FocusRing {
                inset: true
                active: row.activeFocus || row.reordering
                color: row.reordering ? Theme.selectionIndicator : Theme.focusRing
                radius: rowBackground.radius
            }
        }

        contentItem: Item {
            Text {
                visible: !row.focusable
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingMedium
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spacingSmall
                text: row.entry.label
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.weight: Theme.fontWeightHeavy
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Theme.eyebrowLetterSpacing
                color: Theme.textTertiary
            }

            RowLayout {
                visible: row.focusable
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium + Theme.spacingXSmall
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingMedium

                Text {
                    text: row.entry.type === "all" ? Icons.videoLibrary
                                                   : root.libraryIcon(row.entry.library)
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.iconSizeMedium
                    color: row.selected ? Theme.selectionIndicator : Theme.textSecondary
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: row.reordering ? qsTr("Moving %1").arg(row.entry.label) : row.entry.label
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: row.selected || row.activeFocus || row.entry.type === "all"
                                 ? Theme.fontWeightStrong : Theme.fontWeightMedium
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    visible: row.reordering
                    text: Icons.swapVert
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.iconSizeMedium
                    color: Theme.selectionIndicator
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }

    // ========================================
    // Scrim + bar
    // ========================================

    // Soft top scrim so the bar stays legible over any page artwork.
    Rectangle {
        id: barScrim
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.navBarScrimHeight
        opacity: root.shown ? 1.0 : 0.0
        visible: opacity > 0
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.78) }
            GradientStop { position: 0.45; color: Theme.withAlpha(Theme.backgroundPrimary, 0.40) }
            GradientStop { position: 1.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
        }
        Behavior on opacity {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: root.animDuration; easing.type: Theme.easingStandard }
        }
    }

    // Pointer reveal: moving to the top edge brings a receded bar back.
    MouseArea {
        id: pointerReveal
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.navBarInsetTop + Theme.navBarHeight
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    Item {
        id: bar
        objectName: "navigationBar"
        x: Theme.pageMarginX
        width: parent.width - Theme.pageMarginX * 2
        height: Theme.navBarHeight
        y: root.shown ? Theme.navBarInsetTop : -height - Theme.navBarInsetTop
        opacity: root.shown ? 1.0 : 0.0
        visible: opacity > 0

        Behavior on y {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: root.animDuration; easing.type: Theme.easingEmphasized }
        }
        Behavior on opacity {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: root.animDuration; easing.type: Theme.easingStandard }
        }

        // Brand
        Row {
            id: brand
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(14 * Theme.layoutScale)

            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(44 * Theme.layoutScale)
                height: width
                source: "qrc:/images/app/logo_trans.svg"
                sourceSize.width: width * 2
                sourceSize.height: height * 2
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "bloom"
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightHeavy
                font.letterSpacing: Theme.displayLetterSpacing
                color: Theme.textPrimary
            }
        }

        // Page tabs. Scrolls horizontally when there are many libraries.
        Flickable {
            id: tabStrip
            anchors.left: brand.right
            anchors.leftMargin: Math.round(56 * Theme.layoutScale)
            anchors.right: utilities.left
            anchors.rightMargin: Theme.spacingLarge
            anchors.verticalCenter: parent.verticalCenter
            // Room for the focus ring above and below the pills.
            height: Theme.navBarItemHeight + (Theme.focusRingGap + Theme.focusRingWidth) * 2 + 4
            contentWidth: tabRow.implicitWidth + Theme.spacingLarge
            contentHeight: height
            interactive: contentWidth > width
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            clip: contentWidth > width

            Behavior on contentX {
                enabled: Theme.uiAnimationsEnabled
                NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingStandard }
            }

            Row {
                id: tabRow
                x: Theme.focusRingGap + Theme.focusRingWidth + 2
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(8 * Theme.layoutScale)

                Repeater {
                    id: leadingRepeater
                    model: root.leadingItems

                    NavEntry {
                        required property var modelData
                        objectName: "navEntry_" + modelData.id
                        iconGlyph: modelData.icon
                        label: modelData.label
                        selected: root.currentNavigation === modelData.id
                        onActivated: root.handleNavigation(modelData.id)
                    }
                }

                Repeater {
                    id: kindRepeater
                    model: root.kindTabs

                    NavEntry {
                        id: kindEntry
                        required property var modelData
                        readonly property int libraryCount: root.librariesOfKind(modelData.id).length
                        objectName: "navKind_" + modelData.id
                        iconGlyph: modelData.icon
                        label: modelData.label
                        hasMenu: libraryCount > 1
                        selected: root.isCurrentKind(modelData.id)
                        Accessible.description: libraryCount > 1
                                                ? qsTr("Choose a %1 library, or all of them.").arg(modelData.label)
                                                : qsTr("Open %1.").arg(modelData.label)
                        onActivated: root.activateKind(modelData.id, kindEntry)
                    }
                }

                Repeater {
                    id: trailingRepeater
                    model: root.trailingItems

                    NavEntry {
                        required property var modelData
                        objectName: "navEntry_" + modelData.id
                        iconGlyph: modelData.icon
                        label: modelData.label
                        selected: root.currentNavigation === modelData.id
                        onActivated: root.handleNavigation(modelData.id)
                    }
                }
            }
        }

        // Utility cluster
        Row {
            id: utilities
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(14 * Theme.layoutScale)

            NavEntry {
                id: updatesEntry
                objectName: "navEntry_updates"
                kind: "tab"
                visible: root._updatesAvailable
                iconGlyph: Icons.download
                label: qsTr("Update")
                toolTipLabel: qsTr("Update available")
                showBadge: true
                selected: root.currentNavigation === "updates"
                onActivated: root.handleNavigation("updates")
            }

            NavEntry {
                id: settingsEntry
                objectName: "navEntry_settings"
                kind: "icon"
                iconGlyph: Icons.settings
                label: qsTr("Settings")
                selected: root.currentNavigation === "settings"
                onActivated: root.handleNavigation("settings")
            }

            NavEntry {
                id: accountEntry
                objectName: "navEntry_account"
                // Opens the space picker (switch, add an account, sign out).
                readonly property string title: root.spaceName !== "" ? root.spaceName : root.accountName
                kind: title !== "" ? "avatar" : "icon"
                iconGlyph: title !== "" ? title.charAt(0).toUpperCase() : Icons.account
                label: title !== "" ? title : qsTr("Account")
                toolTipLabel: title !== "" ? qsTr("%1: switch space or sign out").arg(title)
                                            : qsTr("Switch space or sign out")
                onActivated: {
                    root.dialogOriginEntry = accountEntry
                    root.spacePickerRequested()
                }
            }

            NavEntry {
                id: powerEntry
                objectName: "navEntry_exit"
                kind: "icon"
                iconGlyph: Icons.power
                label: qsTr("Exit")
                onActivated: {
                    root.dialogOriginEntry = powerEntry
                    root.exitRequested()
                }
            }
        }
    }

    // Pointer clicks outside the open menu close it.
    MouseArea {
        anchors.fill: parent
        enabled: root.menuOpen
        visible: enabled
        onClicked: root.closeMenu()
    }

    // Library menu under the Movies/Series tab. It lives inside the bar's
    // focus scope (not a Popup), so the bar keeps focus and stays shown.
    FocusScope {
        id: menuPanel
        objectName: "navMenu"
        visible: root.menuOpen
        readonly property int padding: Theme.spacingSmall
        readonly property point anchorPoint: root.menuAnchor
                                             ? root.menuAnchor.mapToItem(root, 0, root.menuAnchor.height)
                                             : Qt.point(0, 0)
        width: Math.round(460 * Theme.layoutScale)
        height: menuColumn.implicitHeight + padding * 2
        x: Math.max(Theme.pageMarginX, Math.min(anchorPoint.x, root.width - width - Theme.pageMarginX))
        y: anchorPoint.y + Theme.focusRingGap + Theme.focusRingWidth + Theme.spacingSmall
        opacity: root.menuOpen ? 1.0 : 0.0
        Behavior on opacity {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: Theme.durationShort }
        }

        Accessible.role: Accessible.PopupMenu
        Accessible.name: root.menuKind === "series" ? qsTr("Series libraries") : qsTr("Movie libraries")

        // Swallow pointer events so clicks on the panel do not close it.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        DialogSurface {
            anchors.fill: parent
            floating: true
        }

        Column {
            id: menuColumn
            x: menuPanel.padding
            y: menuPanel.padding
            width: menuPanel.width - menuPanel.padding * 2
            spacing: Math.round(4 * Theme.layoutScale)

            Repeater {
                id: menuRepeater
                model: root.currentMenuEntries
                delegate: MenuRow {}
            }
        }
    }

    onActiveFocusChanged: {
        if (!activeFocus) {
            finishReorder()
            if (menuOpen)
                closeMenu()
        }
    }

    // ========================================
    // Accessibility
    // ========================================

    Accessible.role: Accessible.MenuBar
    Accessible.name: qsTr("Navigation")
}
