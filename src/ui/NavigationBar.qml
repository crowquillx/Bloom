import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * NavigationBar - Bloom's floating top navigation.
 *
 * - Floats over the top of every page on its own soft scrim: brand on the
 *   left, page tabs (Home, each library, Search), and a utility cluster
 *   (update, Settings, space/account, power) on the right. Hero pages draw
 *   under it; other pages start below Theme.navBarReservedHeight.
 * - The current page shows the selection fill; the focused entry shows the
 *   shared FocusRing. Selection and focus stay distinct.
 * - Pages recede the bar (`recessed`) once the user scrolls into content. It
 *   returns whenever focus or the pointer comes back to it.
 * - Left/Right walk every entry in order, Down/Escape return focus to the
 *   page, Enter/Space activate, Shift+Enter starts library reordering
 *   (Left/Right then move the library, Enter finishes).
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
    readonly property bool shown: !recessed || root.activeFocus || pointerReveal.containsMouse

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
        if (reorderingIndex >= libraryItems.length) {
            reorderingIndex = libraryItems.length - 1
        }
    }

    /// Current active navigation item
    property string currentNavigation: "home"
    /// Current active library id (derived from currentNavigation when applicable)
    property string currentLibraryId: ""
    /// Connection that owns the open library; merged spaces list the same
    /// library ID once per account on a shared server.
    property string currentLibraryConnectionId: ""

    function isCurrentLibrary(library) {
        if (!library || currentNavigation !== ("library:" + (library.itemId || "")))
            return false
        return !library.connectionId || currentLibraryConnectionId === ""
                || library.connectionId === currentLibraryConnectionId
    }

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

    function activateLibrary(index) {
        var library = libraryItems[index]
        if (!library)
            return
        var hadFocus = root.activeFocus
        currentNavigation = "library:" + (library.itemId || "")
        currentLibraryId = library.itemId || ""
        currentLibraryConnectionId = library.connectionId || ""
        // A merged library opens on the connection that owns it.
        libraryRouter.run(library.connectionId || "", function() {
            root.libraryRequested(library.itemId || "", library.name || "")
        })
        if (hadFocus)
            Qt.callLater(restoreMainContentFocus)
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
        for (i = 0; i < libraryRepeater.count; ++i) {
            var libraryEntry = libraryRepeater.itemAt(i)
            if (libraryEntry && libraryEntry.visible)
                entries.push(libraryEntry)
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
            for (var i = 0; i < libraryItems.length; ++i) {
                if (isCurrentLibrary(libraryItems[i])) {
                    var libraryEntry = libraryRepeater.itemAt(i)
                    if (libraryEntry) {
                        libraryEntry.forceActiveFocus()
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
        SidebarSettings.libraryOrder = savedOrderWith(reordered.map(function (v) { return v.itemId }))
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
        property bool reordering: false
        property int libraryIndex: -1

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
                var delta = event.key === Qt.Key_Left ? -1 : 1
                if (root.reorderModeActive) {
                    // While reordering, Left/Right move the library being reordered.
                    if (entry.reordering) {
                        root.reorderLibrary(entry.libraryIndex,
                                            Math.max(0, Math.min(root.libraryItems.length - 1, entry.libraryIndex + delta)))
                    }
                } else {
                    root.moveFocus(entry, delta)
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                root.activateKeyboardNavigation()
                root.finishReorder()
                root.restoreMainContentFocus()
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
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

        ToolTip.visible: hovered && entry.kind !== "tab" && toolTipLabel !== ""
        ToolTip.text: toolTipLabel
        ToolTip.delay: 400

        background: Rectangle {
            id: entryBackground
            radius: height / 2
            color: {
                if (entry.reordering || entry.down) return Theme.surfaceInteractivePressed
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
                active: entry.activeFocus || entry.reordering
                color: entry.reordering ? Theme.selectionIndicator : Theme.focusRing
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
                    text: entry.reordering ? qsTr("Moving %1").arg(entry.label) : entry.label
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeMedium
                    font.weight: entry.selected || entry.activeFocus ? Theme.fontWeightStrong : Theme.fontWeightMedium
                    color: entry.selected || entry.activeFocus || entry.hovered ? Theme.textPrimary : Theme.textSecondary
                    Layout.alignment: Qt.AlignVCenter
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
                    id: libraryRepeater
                    model: root.libraryItems

                    NavEntry {
                        required property var modelData
                        required property int index
                        objectName: "navLibrary_" + index
                        iconGlyph: root.libraryIcon(modelData)
                        label: modelData.name || ""
                        toolTipLabel: modelData.sourceLabel ? qsTr("%1 · %2").arg(modelData.name || "").arg(modelData.sourceLabel)
                                                            : (modelData.name || "")
                        selected: root.isCurrentLibrary(modelData)
                        reordering: root.reorderModeActive && root.reorderingIndex === index
                        libraryIndex: index
                        Accessible.description: reordering ? qsTr("Left and Right move the library, Enter finishes.")
                                              : root.mergedLibraries
                                                ? qsTr("Open library %1 from %2.").arg(modelData.name || "").arg(modelData.sourceLabel || "")
                                                : qsTr("Open library %1. Shift+Enter to reorder.").arg(modelData.name || "")

                        onActivated: function(withShift) {
                            if (withShift && !root.mergedLibraries) {
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

    onActiveFocusChanged: {
        if (!activeFocus)
            finishReorder()
    }

    // ========================================
    // Accessibility
    // ========================================

    Accessible.role: Accessible.MenuBar
    Accessible.name: qsTr("Navigation")
}
