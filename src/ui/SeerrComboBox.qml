import QtQuick
import QtQuick.Controls

import BloomUI

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
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        leftPadding: Theme.spacingSmall
        rightPadding: Theme.spacingXLarge
        elide: Text.ElideRight
    }

    indicator: Text {
        text: "▼"
        font.pixelSize: Theme.fontSizeSmall
        font.family: Theme.fontPrimary
        color: Theme.textSecondary
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingMedium
        anchors.verticalCenter: parent.verticalCenter
    }

    background: Rectangle {
        implicitHeight: Theme.buttonHeightSmall
        radius: Theme.radiusSmall
        color: Theme.inputBackground
        border.color: control.activeFocus ? Theme.focusBorder : Theme.inputBorder
        border.width: control.activeFocus ? 2 : 1
        opacity: control.enabled ? 1.0 : 0.5
    }

    delegate: ItemDelegate {
        required property var modelData
        width: control.width
        enabled: control.enabled
        readonly property bool isCurrent: ListView.isCurrentItem

        contentItem: Text {
            text: control.resolveOptionText(modelData)
            color: isCurrent ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: isCurrent ? Theme.buttonPrimaryBackground : "transparent"
            radius: Theme.radiusSmall
        }
    }

    popup: Popup {
        y: control.height + 5
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight, 280)
        padding: 1
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

        background: Rectangle {
            color: Theme.cardBackground
            border.color: Theme.focusBorder
            border.width: 1
            radius: Theme.radiusSmall
        }
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
