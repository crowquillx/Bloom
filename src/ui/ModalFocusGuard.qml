import QtQuick
import QtQuick.Templates as T

/**
 * ModalFocusGuard - keeps keyboard focus inside an open modal popup.
 *
 * Pages behind a modal restore their own focus after async loads and stack
 * transitions. A restore that lands after the modal opens would strand
 * keyboard and controller users outside it, so while the popup is open any
 * focus that escapes is pulled back to the last focused item inside it, or
 * to fallbackItem. A popup stacked above this one keeps its focus.
 */
QtObject {
    id: guard

    required property T.Popup popup
    /// Takes focus back when no earlier focused item inside the popup is usable.
    property Item fallbackItem: null

    property Item lastFocusedItem: null
    readonly property bool active: popup !== null && popup.opened
    // var, not Window: a plain QQuickWindow (QQuickView) is not a QML Window.
    readonly property var window: active && popup.contentItem ? popup.contentItem.Window.window : null

    // Forget the previous opening.
    onActiveChanged: lastFocusedItem = null

    property Connections windowConnections: Connections {
        target: guard.window

        function onActiveFocusItemChanged() {
            if (guard.popup.activeFocus) {
                guard.lastFocusedItem = guard.window.activeFocusItem
                return
            }
            Qt.callLater(guard.reclaim)
        }
    }

    function popupOverlay() {
        // Popup items are children of the window overlay.
        var popupItem = popup.contentItem ? popup.contentItem.parent : null
        return popupItem ? popupItem.parent : null
    }

    function insideAnotherPopup(item) {
        var overlay = popupOverlay()
        for (var current = item; overlay && current; current = current.parent) {
            if (current.parent === overlay)
                return true
        }
        return false
    }

    function reclaim() {
        if (!active || popup.activeFocus || !window || !window.active)
            return
        if (insideAnotherPopup(window.activeFocusItem))
            return
        var item = lastFocusedItem && lastFocusedItem.visible && lastFocusedItem.enabled
                ? lastFocusedItem : fallbackItem
        if (item)
            item.forceActiveFocus(Qt.PopupFocusReason)
        else
            popup.forceActiveFocus(Qt.PopupFocusReason)
    }
}
