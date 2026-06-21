pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import BloomUI

ComboBox {
    id: control

    property string placeholderText: ""
    property bool _closedFromSelection: false

    signal selectionAccepted(int index)

    focusPolicy: Qt.StrongFocus
    font.pixelSize: Theme.fontSizeBody
    font.family: Theme.fontPrimary

    function optionTextAt(index) {
        if (index < 0 || !model)
            return ""

        var item = null
        if (typeof model.get === "function") {
            item = model.get(index)
        } else if (model.length !== undefined) {
            item = model[index]
        }

        return optionText(item)
    }

    function optionText(item) {
        if (item === null || item === undefined)
            return ""
        if (textRole.length > 0 && item[textRole] !== undefined && item[textRole] !== null)
            return String(item[textRole])
        return String(item)
    }

    function selectedText() {
        var resolved = optionTextAt(currentIndex)
        if (resolved.length > 0)
            return resolved
        if (currentText && currentText.length > 0)
            return currentText
        if (displayText && displayText.length > 0)
            return displayText
        return placeholderText
    }

    background: Rectangle {
        implicitHeight: Theme.buttonHeightSmall
        radius: Theme.radiusSmall
        color: Theme.inputBackground
        border.color: control.activeFocus ? Theme.focusBorder : Theme.inputBorder
        border.width: control.activeFocus ? 2 : 1
        opacity: control.enabled ? 1.0 : 0.5
    }

    contentItem: Text {
        text: control.selectedText()
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

    delegate: ItemDelegate {
        id: comboDelegate

        required property int index
        required property var modelData

        width: control.width
        highlighted: ListView.isCurrentItem || control.highlightedIndex === comboDelegate.index

        contentItem: Text {
            text: control.optionText(comboDelegate.modelData)
            color: comboDelegate.highlighted ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: comboDelegate.highlighted ? Theme.buttonPrimaryBackground : "transparent"
            radius: Theme.radiusSmall
        }
    }

    popup: Popup {
        y: control.height + 5
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight, Math.round(360 * Theme.layoutScale))
        padding: 1
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        onOpened: {
            settingsComboPopupList.currentIndex = control.highlightedIndex >= 0 ? control.highlightedIndex : control.currentIndex
            settingsComboPopupList.forceActiveFocus()
        }

        onClosed: {
            if (control._closedFromSelection) {
                control._closedFromSelection = false
                if (KeyNavigation.down) {
                    Qt.callLater(function() { KeyNavigation.down.forceActiveFocus() })
                } else {
                    control.forceActiveFocus()
                }
            } else {
                control.forceActiveFocus()
            }
        }

        contentItem: ListView {
            id: settingsComboPopupList
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.model : null
            currentIndex: control.highlightedIndex >= 0 ? control.highlightedIndex : control.currentIndex
            ScrollIndicator.vertical: ScrollIndicator { }

            delegate: ItemDelegate {
                id: popupDelegate

                required property int index
                required property var modelData

                width: control.width
                highlighted: ListView.isCurrentItem

                contentItem: Text {
                    text: control.optionText(popupDelegate.modelData)
                    color: popupDelegate.highlighted ? Theme.textPrimary : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    color: popupDelegate.highlighted ? Theme.buttonPrimaryBackground : "transparent"
                    radius: Theme.radiusSmall
                }

                onClicked: {
                    control.currentIndex = popupDelegate.index
                    control.selectionAccepted(popupDelegate.index)
                    control._closedFromSelection = true
                    control.popup.close()
                }
            }

            Keys.onReturnPressed: function(event) {
                control.currentIndex = currentIndex
                control.selectionAccepted(currentIndex)
                control._closedFromSelection = true
                control.popup.close()
                event.accepted = true
            }

            Keys.onEnterPressed: function(event) {
                control.currentIndex = currentIndex
                control.selectionAccepted(currentIndex)
                control._closedFromSelection = true
                control.popup.close()
                event.accepted = true
            }

            Keys.onEscapePressed: function(event) {
                control.popup.close()
                event.accepted = true
            }
        }

        background: Rectangle {
            color: Theme.cardBackground
            border.color: Theme.focusBorder
            border.width: 1
            radius: Theme.radiusSmall
        }
    }

    Keys.onReturnPressed: function(event) {
        if (!popup.visible) {
            popup.open()
            event.accepted = true
        }
    }

    Keys.onEnterPressed: function(event) {
        if (!popup.visible) {
            popup.open()
            event.accepted = true
        }
    }

    Keys.onUpPressed: function(event) {
        if (!popup.visible && KeyNavigation.up) {
            KeyNavigation.up.forceActiveFocus()
            event.accepted = true
        }
    }

    Keys.onDownPressed: function(event) {
        if (!popup.visible && KeyNavigation.down) {
            KeyNavigation.down.forceActiveFocus()
            event.accepted = true
        }
    }
}
