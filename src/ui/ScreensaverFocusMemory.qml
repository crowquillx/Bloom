import QtQuick

/**
 * ScreensaverFocusMemory - returns keyboard focus to the page after the
 * screensaver.
 *
 * It follows the window's focus continuously, so the remembered item never
 * depends on whether the overlay took focus before the activation handler
 * ran. While frozen (screensaver showing) nothing is recorded. restore()
 * focuses the remembered item; when that item was destroyed, hidden or
 * disabled meanwhile (a Home row reloaded, the hero rotated to a series and
 * hid Play), `fallback` restores the page's own focus instead.
 */
QtObject {
    id: memory

    /// Window whose focus is remembered. var: a QQuickView is not a QML Window.
    property var window: null
    /// Focus inside this item (the screensaver overlay) is never remembered.
    property Item overlay: null
    /// Called when the remembered item cannot take focus back.
    property var fallback: null

    property Item lastFocusedItem: null
    property bool frozen: false

    function contains(ancestor, item) {
        for (var current = item; ancestor && current; current = current.parent) {
            if (current === ancestor)
                return true
        }
        return false
    }

    function record() {
        if (frozen || !window)
            return
        var item = window.activeFocusItem
        if (!item || item === window.contentItem || contains(overlay, item))
            return
        lastFocusedItem = item
    }

    function freeze() {
        record()
        frozen = true
    }

    function restore() {
        var item = lastFocusedItem
        var usable = item && item.visible && item.enabled
        if (usable)
            item.forceActiveFocus()
        // An inactive window has no active focus item; the focus chain set
        // above still takes effect when it is activated again.
        var windowActive = window && window.active !== false
        if ((!usable || (windowActive && !item.activeFocus)) && typeof fallback === "function")
            fallback()
        frozen = false
        record()
    }

    property Connections windowConnections: Connections {
        target: memory.window
        function onActiveFocusItemChanged() { memory.record() }
    }
}
