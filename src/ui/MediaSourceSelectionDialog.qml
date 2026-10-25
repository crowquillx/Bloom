import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

FocusScope {
    id: rootScope
    anchors.fill: parent
    readonly property bool opened: dialog.visible

    function openForRequest(newRequestId, newDialogModel, newRestoreFocusTarget) {
        dialog.openForRequest(newRequestId, newDialogModel, newRestoreFocusTarget)
    }

    Dialog {
        id: dialog
        modal: true
        focus: true
        // The Loader hosting this file has no size, so center on the window overlay.
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.round(820 * Theme.layoutScale)
        height: Math.round(720 * Theme.layoutScale)
        padding: Theme.dialogPadding
        topPadding: Theme.spacingLarge

        property string requestId: ""
        property var dialogModel: ({})
        property var optionsModel: []
        property Item restoreFocusTarget: null
        property bool acceptedSelection: false
        property string previousNavigationMode: "pointer"

        function setKeyboardNavigationMode() {
            if (typeof InputModeManager !== "undefined") {
                previousNavigationMode = InputModeManager.pointerActive ? "pointer" : "keyboard"
                InputModeManager.setNavigationMode("keyboard")
                InputModeManager.hideCursor(true)
            }
        }

        function restorePreviousNavigationMode() {
            if (typeof InputModeManager !== "undefined") {
                InputModeManager.setNavigationMode(previousNavigationMode)
                InputModeManager.hideCursor(previousNavigationMode !== "pointer")
            }
        }

        function dialogTitle() {
            return dialogModel && dialogModel.title
                    ? dialogModel.title
                    : qsTr("Select Version")
        }

        function dialogSubtitle() {
            return dialogModel && dialogModel.subtitle ? dialogModel.subtitle : ""
        }

        function optionId(option) {
            if (!option) return ""
            return option.mediaSourceId || option.id || option.sourceId || ""
        }

        function optionTitle(option) {
            if (!option) return qsTr("Unknown Version")
            return option.title || option.name || option.displayTitle || qsTr("Unknown Version")
        }

        function optionSubtitle(option) {
            if (!option) return ""
            return option.subtitle || option.description || option.secondaryText || ""
        }

        function openForRequest(newRequestId, newDialogModel, newRestoreFocusTarget) {
            requestId = newRequestId || ""
            dialogModel = newDialogModel || {}
            optionsModel = dialogModel && dialogModel.options ? dialogModel.options : (newDialogModel || [])
            restoreFocusTarget = newRestoreFocusTarget && newRestoreFocusTarget.forceActiveFocus
                    ? newRestoreFocusTarget
                    : null
            acceptedSelection = false
            open()
            setKeyboardNavigationMode()
            Qt.callLater(focusInitialOption)
        }

        function closeWithoutSelection() {
            if (!visible) {
                return
            }
            acceptedSelection = false
            close()
        }

        function ensureOptionVisible(item) {
            if (!item || !contentScroll || !contentScroll.contentItem) {
                return
            }

            var flick = contentScroll.contentItem
            var pointInContent = item.mapToItem(optionColumn, 0, 0)
            var itemTop = pointInContent.y
            var itemBottom = itemTop + item.height
            var viewTop = flick["contentY"]
            var viewBottom = viewTop + flick.height

            if (itemTop < viewTop) {
                flick["contentY"] = Math.max(0, itemTop - Theme.spacingSmall)
            } else if (itemBottom > viewBottom) {
                var maxY = Math.max(0, flick["contentHeight"] - flick.height)
                flick["contentY"] = Math.min(maxY, itemBottom - flick.height + Theme.spacingSmall)
            }
        }

        function focusInitialOption() {
            if (!visible || optionRepeater.count === 0) {
                dialog.forceActiveFocus()
                return
            }

            var fallbackIndex = 0
            for (var i = 0; i < optionsModel.length; ++i) {
                if (optionsModel[i] && optionsModel[i].selected) {
                    fallbackIndex = i
                    break
                }
            }

            var initialItem = optionRepeater.itemAt(fallbackIndex)
            if (!initialItem) {
                initialItem = optionRepeater.itemAt(0)
            }
            if (initialItem) {
                initialItem.forceActiveFocus()
                ensureOptionVisible(initialItem)
            }
        }

        onClosed: {
            if (!acceptedSelection && requestId) {
                PlayerController.cancelPendingPlaybackRequest(requestId)
            }

            restorePreviousNavigationMode()
            var focusTarget = restoreFocusTarget
            requestId = ""
            dialogModel = ({})
            optionsModel = []
            restoreFocusTarget = null
            acceptedSelection = false

            if (focusTarget) {
                Qt.callLater(function() {
                    if (focusTarget) {
                        focusTarget.forceActiveFocus()
                    }
                })
            }
        }

        Shortcut {
            sequences: ["Escape", "Back"]
            enabled: dialog.visible
            onActivated: dialog.closeWithoutSelection()
        }

        background: DialogSurface {}
        Overlay.modal: ModalScrim {}

        header: Item {
            implicitHeight: dialogHeaderColumn.implicitHeight + Theme.dialogPadding + Theme.spacingMedium

            Column {
                id: dialogHeaderColumn
                anchors.left: parent.left
                anchors.leftMargin: Theme.dialogPadding
                anchors.right: parent.right
                anchors.rightMargin: Theme.dialogPadding
                anchors.bottom: parent.bottom
                spacing: Theme.spacingSmall

                Text {
                    text: dialog.dialogTitle()
                    font.pixelSize: Theme.fontSizeTitle
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightHeavy
                    color: Theme.textPrimary
                }

                Text {
                    visible: text.length > 0
                    text: dialog.dialogSubtitle()
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                }
            }
        }

        contentItem: FocusScope {
            implicitWidth: Math.round(680 * Theme.layoutScale)
            implicitHeight: Math.round(560 * Theme.layoutScale)

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                    dialog.closeWithoutSelection()
                    event.accepted = true
                }
            }

            ScrollView {
                id: contentScroll
                anchors.fill: parent
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                Column {
                    id: optionColumn
                    width: contentScroll.availableWidth
                    spacing: Theme.spacingMedium

                    Repeater {
                        id: optionRepeater
                        model: dialog.optionsModel

                        delegate: Button {
                            id: optionButton
                            required property int index
                            readonly property var optionData: dialog.optionsModel[index]
                            readonly property bool selectedOption: !!(optionData && optionData.selected)

                            width: optionColumn.width
                            height: implicitHeight
                            focusPolicy: Qt.StrongFocus
                            activeFocusOnTab: true
                            hoverEnabled: true

                            KeyNavigation.up: index > 0 ? optionRepeater.itemAt(index - 1) : null
                            KeyNavigation.down: index + 1 < optionRepeater.count ? optionRepeater.itemAt(index + 1) : cancelButton

                            Keys.onReturnPressed: function(event) {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                clicked()
                                event.accepted = true
                            }
                            Keys.onEnterPressed: function(event) {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                clicked()
                                event.accepted = true
                            }
                            Keys.onSpacePressed: function(event) {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                clicked()
                                event.accepted = true
                            }

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    dialog.ensureOptionVisible(optionButton)
                                }
                            }

                            onClicked: {
                                var mediaSourceId = dialog.optionId(optionData)
                                if (!mediaSourceId) {
                                    return
                                }
                                dialog.acceptedSelection = true
                                PlayerController.confirmPlaybackVersion(dialog.requestId, mediaSourceId)
                                dialog.close()
                            }

                            leftPadding: Theme.spacingLarge
                            rightPadding: Theme.spacingLarge
                            topPadding: Theme.spacingMedium
                            bottomPadding: Theme.spacingMedium

                            background: Rectangle {
                                radius: Theme.radiusMedium
                                color: {
                                    if (optionButton.down) return Theme.surfaceInteractivePressed
                                    if (optionButton.hovered || optionButton.activeFocus) return Theme.surfaceInteractiveHover
                                    return optionButton.selectedOption ? Theme.selectionBackground : Theme.surfaceInteractive
                                }

                                Text {
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.spacingLarge
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: optionButton.selectedOption
                                    text: Icons.check
                                    font.family: Theme.fontIcon
                                    font.pixelSize: Theme.fontSizeIcon
                                    color: Theme.selectionIndicator
                                }

                                FocusRing {
                                    inset: true
                                    active: optionButton.activeFocus
                                    radius: parent.radius
                                }
                            }

                            contentItem: Column {
                                spacing: Theme.spacingSmall

                                Text {
                                    text: dialog.optionTitle(optionButton.optionData)
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    font.weight: Theme.fontWeightStrong
                                    color: Theme.textPrimary
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    visible: text.length > 0
                                    text: dialog.optionSubtitle(optionButton.optionData)
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textSecondary
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }

        footer: FocusScope {
            implicitHeight: Theme.buttonHeightLarge + Theme.dialogPadding + Theme.spacingMedium

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                    dialog.closeWithoutSelection()
                    event.accepted = true
                }
            }

            RowLayout {
                anchors.right: parent.right
                anchors.rightMargin: Theme.dialogPadding
                anchors.top: parent.top
                anchors.topMargin: Theme.spacingMedium
                spacing: Theme.controlSpacing

                ActionButton {
                    id: cancelButton
                    variant: "secondary"
                    text: qsTr("Cancel")
                    implicitHeight: Theme.buttonHeightLarge
                    KeyNavigation.up: optionRepeater.count > 0 ? optionRepeater.itemAt(optionRepeater.count - 1) : null

                    Keys.onReturnPressed: function(event) {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        dialog.closeWithoutSelection()
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        dialog.closeWithoutSelection()
                        event.accepted = true
                    }

                    onClicked: dialog.closeWithoutSelection()


                }
            }
        }
    }
}
