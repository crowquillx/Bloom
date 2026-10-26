pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import BloomUI

ComboBox {
    id: control

    property string placeholderText: ""
    /// Inside a SettingsRow the row draws focus, so the combo only keeps its fill.
    property bool showFocusRing: !(parent && parent.objectName === "settingsRowTrailing")
    property bool _closedFromSelection: false

    signal selectionAccepted(int index)

    focusPolicy: Qt.StrongFocus
    implicitWidth: Math.round(380 * Theme.layoutScale)
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
        id: comboBackground
        implicitHeight: Theme.buttonHeightSmall
        radius: Theme.radiusControl
        color: control.hovered || control.activeFocus ? Theme.surfaceInteractiveHover : Theme.surfaceInteractive
        border.width: Theme.borderWidth
        border.color: control.activeFocus ? Theme.surfaceBorderStrong : Theme.surfaceBorder
        opacity: control.enabled ? 1.0 : Theme.disabledOpacity

        FocusRing {
            active: control.showFocusRing && (control.activeFocus || control.popup.visible)
            radius: comboBackground.radius
        }
    }

    contentItem: Text {
        text: control.selectedText()
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontPrimary
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

    delegate: ItemDelegate {
        id: comboDelegate

        required property int index
        required property var modelData

        width: control.width
        highlighted: ListView.isCurrentItem || control.highlightedIndex === comboDelegate.index

        contentItem: Text {
            text: control.optionText(comboDelegate.modelData)
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: comboDelegate.highlighted ? Theme.surfaceInteractiveHover : "transparent"
            radius: Theme.radiusSmall
        }
    }

    popup: Popup {
        y: control.height + Theme.spacingSmall
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, Math.round(560 * Theme.layoutScale))
        padding: Theme.spacingSmall
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

                width: control.width - Theme.spacingSmall * 2
                implicitHeight: Theme.navBarItemHeight
                highlighted: ListView.isCurrentItem

                contentItem: Row {
                    spacing: Theme.spacingSmall
                    Text {
                        width: parent.width - checkGlyph.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: control.optionText(popupDelegate.modelData)
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        font.weight: popupDelegate.index === control.currentIndex ? Theme.fontWeightStrong : Theme.fontWeightRegular
                        elide: Text.ElideRight
                    }
                    Text {
                        id: checkGlyph
                        anchors.verticalCenter: parent.verticalCenter
                        text: popupDelegate.index === control.currentIndex ? Icons.check : ""
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeIcon
                        color: Theme.selectionIndicator
                    }
                }

                background: Rectangle {
                    color: popupDelegate.highlighted ? Theme.surfaceInteractiveHover : "transparent"
                    radius: Theme.radiusSmall
                    FocusRing { inset: true; active: popupDelegate.highlighted; radius: parent.radius }
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

        background: DialogSurface { floating: true; radius: Theme.radiusMedium }
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
