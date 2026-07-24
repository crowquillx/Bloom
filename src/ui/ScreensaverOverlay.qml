import QtQuick
import QtQuick.Window
import QtQuick.Effects
import BloomUI

Item {
    id: root
    anchors.fill: parent
    visible: active
    focus: visible

    property bool active: ScreensaverController.active
    property var focusWindow: null
    property var items: []
    property string itemsConnectionId: ""
    property int currentIndex: -1
    property int placementIndex: 0
    property bool showBackdropA: true
    property string backdropAUrl: ""
    property string backdropBUrl: ""
    property var savedFocusItem: null
    property var restoreWindow: null
    property bool restoreWindowEligible: true
    property string savedNavigationMode: "pointer"
    property string currentBackdropUrl: showBackdropA ? backdropAUrl : backdropBUrl
    property var currentItem: currentIndex >= 0 && currentIndex < items.length ? items[currentIndex] : ({})
    property real logoX: Math.round(width * 0.08)
    property real logoY: Math.round(height * 0.10)
    property real logoDx: 2.4
    property real logoDy: 1.8

    readonly property bool artworkMode: ScreensaverController.mode !== "black"
    readonly property bool bouncingLogoMode: ScreensaverController.mode === "bouncingLogo"
    readonly property var placements: ["bottomLeft", "topRight", "bottomRight", "topLeft", "bottomCenter", "topCenter"]

    onVisibleChanged: {
        if (visible) {
            restoreWindow = restoreWindow || focusWindow || root.Window.window || null
            if (typeof InputModeManager !== "undefined") {
                savedNavigationMode = InputModeManager.pointerActive ? "pointer" : "keyboard"
                InputModeManager.setNavigationMode("keyboard")
                InputModeManager.hideCursor(true)
            }
            const activeConnectionId = LibraryService.getActiveConnectionId()
            if (itemsConnectionId !== activeConnectionId) {
                resetArtwork()
            }
            if (artworkMode && items.length === 0) {
                LibraryService.getScreensaverItems(80)
            }
            selectNextItem()
            cycleTimer.restart()
            if (bouncingLogoMode) {
                bounceTimer.restart()
            }
        } else {
            cycleTimer.stop()
            bounceTimer.stop()
            Qt.callLater(function() {
                if (typeof InputModeManager !== "undefined") {
                    InputModeManager.setNavigationMode(savedNavigationMode)
                    InputModeManager.hideCursor(savedNavigationMode !== "pointer")
                }
                if (restoreWindowEligible && restoreWindow && typeof restoreWindow.requestActivate === "function") {
                    restoreWindow.requestActivate()
                }
                if (savedFocusItem && savedFocusItem.parent && typeof savedFocusItem.forceActiveFocus === "function") {
                    savedFocusItem.forceActiveFocus()
                }
                savedFocusItem = null
            })
        }
    }

    onWidthChanged: clampLogo()
    onHeightChanged: clampLogo()

    Connections {
        target: ScreensaverController
        function onActiveChanged() {
            if (root.active) {
                restoreWindow = restoreWindow || focusWindow || root.Window.window || null
                savedFocusItem = root.focusWindow
                        ? root.focusWindow.activeFocusItem
                        : root.Window.activeFocusItem
            }
        }
    }

    Connections {
        target: AuthenticationService
        function onLoginSuccess() {
            root.resetArtwork()
            if (root.visible && root.artworkMode) {
                LibraryService.getScreensaverItems(80)
            }
        }
        function onLoggedOut() { root.resetArtwork() }
        function onSessionExpired() { root.resetArtwork() }
        function onSessionExpiredAfterPlayback() { root.resetArtwork() }
    }

    function resetArtwork() {
        root.items = []
        root.itemsConnectionId = ""
        root.currentIndex = -1
        root.placementIndex = 0
        root.showBackdropA = true
        root.backdropAUrl = ""
        root.backdropBUrl = ""
    }

    function artworkUrlFor(artwork, fallbackKind, width) {
        if (!artwork || !artwork.itemId) {
            return ""
        }
        const kind = artwork.kind || fallbackKind
        if (!kind) {
            return ""
        }
        return LibraryService.getCachedArtworkUrlForConnection(
                    artwork.connectionId || "",
                    artwork.itemId,
                    kind,
                    artwork.index || 0,
                    artwork.tag || "",
                    width)
    }

    function backdropUrlFor(item) {
        return item ? artworkUrlFor(item.backdropArtwork, "backdrop", 1920) : ""
    }

    function logoUrlFor(item) {
        return item ? artworkUrlFor(item.logoArtwork, "logo", 700) : ""
    }

    function selectNextItem() {
        if (!artworkMode || items.length === 0) {
            currentIndex = -1
            return
        }

        currentIndex = (currentIndex + 1) % items.length
        placementIndex = (placementIndex + 1) % placements.length
        var nextUrl = backdropUrlFor(currentItem)
        if (showBackdropA) {
            backdropBUrl = nextUrl
        } else {
            backdropAUrl = nextUrl
        }
        showBackdropA = !showBackdropA
        resetLogoPosition()
    }

    function resetLogoPosition() {
        logoX = Math.round(Math.max(Theme.spacingLarge, width * (0.08 + ((placementIndex % 3) * 0.12))))
        logoY = Math.round(Math.max(Theme.spacingLarge, height * (0.10 + ((placementIndex % 2) * 0.16))))
        logoDx = placementIndex % 2 === 0 ? 2.4 : -2.4
        logoDy = placementIndex % 3 === 0 ? 1.8 : -1.8
        clampLogo()
    }

    function clampLogo() {
        var maxX = Math.max(0, width - bouncingLogo.width - Theme.spacingLarge)
        var maxY = Math.max(0, height - bouncingLogo.height - Theme.spacingLarge)
        logoX = Math.max(Theme.spacingLarge, Math.min(maxX, logoX))
        logoY = Math.max(Theme.spacingLarge, Math.min(maxY, logoY))
    }

    function metadataPositionFor(placement, blockWidth, blockHeight) {
        var margin = Math.round(64 * Theme.layoutScale)
        var centeredX = Math.round(Math.max(margin, (root.width - blockWidth) / 2))
        var rightX = Math.max(margin, root.width - blockWidth - margin)
        var bottomY = Math.max(margin, root.height - blockHeight - margin)
        if (placement === "topLeft")
            return { x: margin, y: margin }
        if (placement === "topRight")
            return { x: rightX, y: margin }
        if (placement === "bottomRight")
            return { x: rightX, y: bottomY }
        if (placement === "topCenter")
            return { x: centeredX, y: margin }
        if (placement === "bottomCenter")
            return { x: centeredX, y: bottomY }
        return { x: margin, y: bottomY }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.visible
        hoverEnabled: enabled
        acceptedButtons: Qt.AllButtons
        preventStealing: true
        z: 1000
        onPressed: function(mouse) { mouse.accepted = true }
        onReleased: function(mouse) { mouse.accepted = true }
        onClicked: function(mouse) { mouse.accepted = true }
        onWheel: function(wheel) { wheel.accepted = true }
    }

    Item {
        anchors.fill: parent
        visible: root.artworkMode
        opacity: 0.55

        Image {
            id: backdropA
            anchors.fill: parent
            source: root.backdropAUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: root.showBackdropA ? 1 : 0
            scale: root.showBackdropA && root.visible ? 1.04 : 1.0
            Behavior on opacity { NumberAnimation { duration: Theme.uiAnimationsEnabled ? 1600 : 0 } }
            Behavior on scale { NumberAnimation { duration: Theme.uiAnimationsEnabled ? 12000 : 0 } }
            layer.enabled: true
            layer.effect: MultiEffect {
                saturation: 0.72
                brightness: -0.18
            }
        }

        Image {
            id: backdropB
            anchors.fill: parent
            source: root.backdropBUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: root.showBackdropA ? 0 : 1
            scale: !root.showBackdropA && root.visible ? 1.04 : 1.0
            Behavior on opacity { NumberAnimation { duration: Theme.uiAnimationsEnabled ? 1600 : 0 } }
            Behavior on scale { NumberAnimation { duration: Theme.uiAnimationsEnabled ? 12000 : 0 } }
            layer.enabled: true
            layer.effect: MultiEffect {
                saturation: 0.72
                brightness: -0.18
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.artworkMode
        color: "black"
        opacity: 0.42
    }

    Column {
        id: metadataBlock
        visible: root.artworkMode && root.currentIndex >= 0
        width: Math.min(Math.round(620 * Theme.layoutScale), root.width - Math.round(128 * Theme.layoutScale))
        spacing: Theme.spacingMedium
        x: root.metadataPositionFor(root.placements[root.placementIndex], width, height).x
        y: root.metadataPositionFor(root.placements[root.placementIndex], width, height).y

        Behavior on x { NumberAnimation { duration: Theme.uiAnimationsEnabled ? 900 : 0; easing.type: Easing.InOutQuad } }
        Behavior on y { NumberAnimation { duration: Theme.uiAnimationsEnabled ? 900 : 0; easing.type: Easing.InOutQuad } }

        Image {
            id: staticLogo
            source: root.logoUrlFor(root.currentItem)
            width: metadataBlock.width
            height: Math.round(150 * Theme.layoutScale)
            fillMode: Image.PreserveAspectFit
            sourceSize.width: 700
            asynchronous: true
            visible: !root.bouncingLogoMode && source.toString().length > 0 && status !== Image.Error
            opacity: 0.82
        }

        Text {
            text: root.currentItem.name || root.currentItem.seriesName || ""
            visible: !root.bouncingLogoMode && !staticLogo.visible
            width: metadataBlock.width
            color: Qt.rgba(1, 1, 1, 0.78)
            font.family: Theme.fontPrimary
            font.bold: true
            font.pixelSize: Math.round(42 * Theme.layoutScale)
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            text: root.currentItem.overview || ""
            width: metadataBlock.width
            color: Qt.rgba(1, 1, 1, 0.62)
            font.family: Theme.fontPrimary
            font.pixelSize: Math.round(20 * Theme.layoutScale)
            lineHeight: 1.15
            wrapMode: Text.WordWrap
            maximumLineCount: 4
            elide: Text.ElideRight
            visible: text.length > 0
        }
    }

    Image {
        id: bouncingLogo
        visible: root.artworkMode && root.bouncingLogoMode && root.currentIndex >= 0
        source: root.logoUrlFor(root.currentItem)
        x: root.logoX
        y: root.logoY
        width: Math.min(Math.round(320 * Theme.layoutScale), root.width * 0.28)
        height: Math.round(130 * Theme.layoutScale)
        fillMode: Image.PreserveAspectFit
        sourceSize.width: 700
        asynchronous: true
        opacity: 0.72
    }

    Text {
        visible: root.artworkMode
                 && root.bouncingLogoMode
                 && root.currentIndex >= 0
                 && (bouncingLogo.source.toString().length === 0 || bouncingLogo.status === Image.Error)
        text: root.currentItem.name || ""
        x: root.logoX
        y: root.logoY
        width: Math.min(Math.round(360 * Theme.layoutScale), root.width * 0.32)
        color: Qt.rgba(1, 1, 1, 0.70)
        font.family: Theme.fontPrimary
        font.bold: true
        font.pixelSize: Math.round(32 * Theme.layoutScale)
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Timer {
        id: cycleTimer
        interval: 12000
        repeat: true
        running: root.visible && root.artworkMode
        onTriggered: root.selectNextItem()
    }

    Timer {
        id: bounceTimer
        interval: 16
        repeat: true
        running: root.visible && root.bouncingLogoMode
        onTriggered: {
            var logoWidth = bouncingLogo.visible ? bouncingLogo.width : Math.min(Math.round(360 * Theme.layoutScale), root.width * 0.32)
            var logoHeight = bouncingLogo.visible ? bouncingLogo.height : Math.round(90 * Theme.layoutScale)
            var minX = Theme.spacingLarge
            var minY = Theme.spacingLarge
            var maxX = Math.max(minX, root.width - logoWidth - Theme.spacingLarge)
            var maxY = Math.max(minY, root.height - logoHeight - Theme.spacingLarge)
            root.logoX += root.logoDx
            root.logoY += root.logoDy
            if (root.logoX <= minX || root.logoX >= maxX) {
                root.logoDx = -root.logoDx
                root.logoX = Math.max(minX, Math.min(maxX, root.logoX))
            }
            if (root.logoY <= minY || root.logoY >= maxY) {
                root.logoDy = -root.logoDy
                root.logoY = Math.max(minY, Math.min(maxY, root.logoY))
            }
        }
    }

    Connections {
        target: LibraryService
        function onCanonicalScreensaverItemsLoaded(connectionId, loadedItems) {
            if (!root.visible || connectionId !== LibraryService.getActiveConnectionId()) {
                return
            }
            root.items = loadedItems || []
            root.itemsConnectionId = connectionId
            root.currentIndex = -1
            root.selectNextItem()
        }

        function onCanonicalScreensaverItemsFailed(connectionId, error) {
            if (root.visible && connectionId === LibraryService.getActiveConnectionId()) {
                root.resetArtwork()
            }
        }
    }
}
