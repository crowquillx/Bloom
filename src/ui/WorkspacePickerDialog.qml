import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * WorkspacePickerDialog - quick switcher for Bloom workspaces.
 *
 * Up/Down walk the workspaces and the actions below them as one column;
 * Enter selects; Escape/Back closes. Focus opens on the active workspace.
 * The caller restores focus in onClosed (rail or page).
 */
Dialog {
    id: root

    /// Show the sign-out action (hidden on the sign-in screen).
    property bool allowSignOut: true
    /// Show "Manage workspaces" (needs the settings screen).
    property bool allowManage: true
    /// Show "Add server or account".
    property bool allowAdd: true

    signal manageRequested()
    signal signOutRequested()

    readonly property var workspaceList: typeof Workspaces !== "undefined" ? Workspaces.profiles : []

    modal: true
    focus: true
    anchors.centerIn: parent
    width: Math.min(Math.round(720 * Theme.layoutScale),
                    Math.max(0, (parent ? parent.width : 0) - Theme.spacingLarge * 2))
    padding: Theme.dialogPadding
    topPadding: Theme.spacingLarge

    background: DialogSurface {}
    Overlay.modal: ModalScrim {}

    function activeIndex() {
        for (var i = 0; i < workspaceList.length; ++i) {
            if (workspaceList[i].active)
                return i
        }
        return 0
    }

    function focusInitial() {
        if (workspaceList.length > 0) {
            workspaceView.currentIndex = activeIndex()
            workspaceView.forceActiveFocus()
        } else {
            var buttons = actionButtons()
            if (buttons.length > 0)
                buttons[0].forceActiveFocus()
        }
    }

    function chooseWorkspace(index) {
        var workspace = workspaceList[index]
        if (!workspace)
            return
        close()
        if (!workspace.active || !AuthenticationService.authenticated)
            Workspaces.switchProfile(workspace.id)
    }

    function actionButtons() {
        var buttons = []
        if (manageButton.visible) buttons.push(manageButton)
        if (addButton.visible) buttons.push(addButton)
        if (signOutButton.visible) buttons.push(signOutButton)
        return buttons
    }

    function moveFromAction(button, delta) {
        var buttons = actionButtons()
        var index = buttons.indexOf(button) + delta
        if (index < 0) {
            if (workspaceList.length > 0) {
                workspaceView.currentIndex = workspaceList.length - 1
                workspaceView.forceActiveFocus()
            }
            return
        }
        if (index < buttons.length)
            buttons[index].forceActiveFocus()
    }

    onOpened: Qt.callLater(focusInitial)

    header: Item {
        implicitHeight: headerColumn.implicitHeight + Theme.dialogPadding

        Column {
            id: headerColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.dialogPadding
            spacing: Theme.spacingSmall

            Text {
                text: qsTr("Workspaces")
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Text {
                width: parent.width
                text: qsTr("Switch between servers and accounts, or browse several together.")
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingSmall

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                event.accepted = true
                root.close()
            }
        }

        ListView {
            id: workspaceView
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Math.round(460 * Theme.layoutScale))
            clip: true
            spacing: Theme.spacingXSmall
            model: root.workspaceList
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            visible: count > 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Workspaces")

            Keys.onDownPressed: function(event) {
                var buttons = root.actionButtons()
                if (currentIndex < count - 1) {
                    incrementCurrentIndex()
                } else if (buttons.length > 0) {
                    buttons[0].forceActiveFocus()
                }
                event.accepted = true
            }
            Keys.onReturnPressed: root.chooseWorkspace(currentIndex)
            Keys.onEnterPressed: root.chooseWorkspace(currentIndex)
            Keys.onSpacePressed: root.chooseWorkspace(currentIndex)

            delegate: ItemDelegate {
                id: workspaceRow
                required property var modelData
                required property int index
                width: workspaceView.width
                height: Math.round(96 * Theme.layoutScale)
                focusPolicy: Qt.NoFocus
                leftPadding: Theme.spacingMedium + Theme.spacingSmall
                rightPadding: Theme.spacingMedium
                readonly property bool focused: workspaceView.activeFocus && workspaceView.currentIndex === index

                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.name + (modelData.active ? ", " + qsTr("current") : "")
                Accessible.description: modelData.summary

                onClicked: root.chooseWorkspace(index)

                background: Rectangle {
                    id: rowBackground
                    radius: Theme.radiusMedium
                    color: workspaceRow.focused ? Theme.surfaceInteractiveHover
                           : (workspaceRow.modelData.active ? Theme.selectionBackground
                              : (workspaceRow.hovered ? Theme.surfaceInteractive : "transparent"))

                    Rectangle {
                        visible: workspaceRow.modelData.active
                        width: Theme.selectionIndicatorWidth
                        height: Math.round(parent.height * 0.44)
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(6 * Theme.layoutScale)
                        anchors.verticalCenter: parent.verticalCenter
                        radius: width / 2
                        color: Theme.selectionIndicator
                    }

                    FocusRing {
                        inset: true
                        active: workspaceRow.focused
                        radius: rowBackground.radius
                    }
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingMedium

                    Text {
                        text: workspaceRow.modelData.merged ? Icons.group : Icons.person
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeIcon
                        color: workspaceRow.modelData.active ? Theme.accentPrimary : Theme.textSecondary
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Math.round(2 * Theme.layoutScale)

                        Text {
                            Layout.fillWidth: true
                            text: workspaceRow.modelData.name
                            elide: Text.ElideRight
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: Theme.fontWeightStrong
                            color: Theme.textPrimary
                        }

                        Text {
                            Layout.fillWidth: true
                            text: workspaceRow.modelData.summary
                            elide: Text.ElideRight
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                        }
                    }

                    MetadataChip {
                        text: workspaceRow.modelData.merged ? qsTr("Merged") : ""
                        Layout.alignment: Qt.AlignVCenter
                    }
                }
            }
        }

        Item { Layout.preferredHeight: Theme.spacingSmall; visible: workspaceView.visible }

        ActionButton {
            id: manageButton
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeightLarge
            visible: root.allowManage
            variant: "secondary"
            iconGlyph: Icons.settings
            text: qsTr("Manage workspaces")
            Keys.onUpPressed: root.moveFromAction(manageButton, -1)
            Keys.onDownPressed: root.moveFromAction(manageButton, 1)
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            onClicked: {
                root.close()
                root.manageRequested()
            }
        }

        ActionButton {
            id: addButton
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeightLarge
            visible: root.allowAdd
            variant: "secondary"
            iconGlyph: Icons.add
            text: qsTr("Add server or account")
            Keys.onUpPressed: root.moveFromAction(addButton, -1)
            Keys.onDownPressed: root.moveFromAction(addButton, 1)
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            onClicked: {
                root.close()
                Workspaces.beginAddConnection("")
            }
        }

        ActionButton {
            id: signOutButton
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeightLarge
            visible: root.allowSignOut
            variant: "destructive"
            iconGlyph: Icons.logout
            text: qsTr("Sign out")
            Keys.onUpPressed: root.moveFromAction(signOutButton, -1)
            Keys.onDownPressed: root.moveFromAction(signOutButton, 1)
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            onClicked: {
                root.close()
                root.signOutRequested()
            }
        }
    }
}
