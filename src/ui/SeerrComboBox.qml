import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * SeerrComboBox - Themed ComboBox with full keyboard and gamepad navigation.
 *
 * Extends the Qt Quick Controls ComboBox with:
 *  - Bloom theme tokens applied to all visual sub-components (content, indicator,
 *    background, delegate, popup).
 *  - Keyboard/gamepad-aware popup: opens on Return/Enter, closes on Escape or an
 *    outside click (Popup.CloseOnEscape | Popup.CloseOnPressOutside).
 *  - Up/Down arrow keys delegate navigation upward/downward to the parent view via
 *    navigateUpRequested / navigateDownRequested signals when the popup is closed.
 *  - resolveOptionText() supports both object models (via textRole) and plain
 *    string/number models (fallback to String(modelData)).
 *
 * Signals:
 *  - navigateUpRequested()        Emitted on Up arrow when popup is closed.
 *  - navigateDownRequested()      Emitted on Down arrow when popup is closed.
 *  - popupOpenedForKeyboardNav()  Emitted when the popup opens so callers can
 *                                 switch InputModeManager to keyboard mode.
 */
ComboBox {
    id: control
    focusPolicy: Qt.StrongFocus
    activeFocusOnTab: true
    Keys.priority: Keys.BeforeItem
    font.pixelSize: Theme.fontSizeBody
    font.family: Theme.fontPrimary

    signal navigateUpRequested()
    signal navigateDownRequested()
    signal popupOpenedForKeyboardNav()

    /**
     * Resolves the display string for a delegate item.
     *
     * Primary path: when textRole is set and modelData is an object that has the
     * named role, returns String(modelData[textRole]).
     *
     * Fallback: when modelData is a plain value (string, number, etc.) and no
     * role-based match is found, returns String(modelData).  Returns "" only when
     * modelData is null or undefined.
     *
     * @param modelData  The raw model item passed to each delegate.
     * @returns          A string suitable for display in the combo or its popup list.
     */
    function resolveOptionText(modelData) {
        if (textRole.length > 0 && modelData && modelData[textRole] !== undefined && modelData[textRole] !== null) {
            return String(modelData[textRole])
        } else if (modelData !== null && modelData !== undefined) {
            return String(modelData)
        }
        return ""
    }

    contentItem: Text {
        text: control.displayText
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontPrimary
        font.weight: Theme.fontWeightMedium
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        leftPadding: Theme.spacingMedium
        rightPadding: Theme.spacingXLarge + Theme.spacingMedium
        elide: Text.ElideRight
    }

    indicator: Text {
        text: Icons.expandMore
        font.pixelSize: Theme.fontSizeIcon
        font.family: Theme.fontIcon
        color: Theme.textSecondary
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingMedium
        anchors.verticalCenter: parent.verticalCenter
    }

    background: Rectangle {
        id: comboBackground
        implicitHeight: Theme.buttonHeightMedium
        radius: Theme.radiusControl
        color: control.hovered || control.activeFocus ? Theme.surfaceInteractiveHover : Theme.surfaceInteractive
        border.width: Theme.borderWidth
        border.color: Theme.surfaceBorder
        opacity: control.enabled ? 1.0 : Theme.disabledOpacity

        FocusRing {
            active: control.activeFocus || control.popup.visible
            radius: comboBackground.radius
        }
    }

    delegate: ItemDelegate {
        id: optionDelegate
        required property var modelData
        required property int index
        width: control.width - Theme.spacingSmall * 2
        implicitHeight: Theme.buttonHeightSmall
        enabled: control.enabled
        readonly property bool isCurrent: ListView.isCurrentItem

        contentItem: RowLayout {
            spacing: Theme.spacingSmall
            Text {
                text: control.resolveOptionText(optionDelegate.modelData)
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                font.weight: optionDelegate.index === control.currentIndex ? Theme.fontWeightStrong : Theme.fontWeightRegular
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                visible: optionDelegate.index === control.currentIndex
                text: Icons.check
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                color: Theme.selectionIndicator
            }
        }

        background: Rectangle {
            color: optionDelegate.isCurrent ? Theme.surfaceInteractiveHover : "transparent"
            radius: Theme.radiusSmall
            FocusRing {
                inset: true
                active: optionDelegate.isCurrent && control.popup.visible
                radius: parent.radius
            }
        }
    }

    popup: Popup {
        y: control.height + Theme.spacingSmall
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, Math.round(420 * Theme.layoutScale))
        padding: Theme.spacingSmall
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        onOpened: {
            control.popupOpenedForKeyboardNav()
            popupList.currentIndex = Math.max(0, control.currentIndex)
            popupList.forceActiveFocus()
        }

        onClosed: control.forceActiveFocus()

        contentItem: ListView {
            id: popupList
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: Math.max(0, control.currentIndex)

            Keys.onReturnPressed: function(event) {
                control.currentIndex = currentIndex
                control.popup.close()
                event.accepted = true
            }

            Keys.onEnterPressed: function(event) {
                control.currentIndex = currentIndex
                control.popup.close()
                event.accepted = true
            }

            Keys.onEscapePressed: control.popup.close()
        }

        background: DialogSurface { floating: true; radius: Theme.radiusMedium }
    }

    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !popup.visible) {
            popup.open()
            event.accepted = true
        }
    }

    Keys.onUpPressed: function(event) {
        if (!popup.visible) {
            navigateUpRequested()
            event.accepted = true
        }
    }

    Keys.onDownPressed: function(event) {
        if (!popup.visible) {
            navigateDownRequested()
            event.accepted = true
        }
    }
}
