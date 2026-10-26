import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * SpacePickerDialog - quick switcher for Bloom spaces.
 *
 * Spaces and the actions below them (Manage spaces, Add a server or account,
 * Sign out) are one list: Up/Down walk it, Enter selects, Escape/Back closes.
 * Focus opens on the current space. The caller restores focus in onClosed
 * (rail or page).
 */
Dialog {
    id: root

    /// Show the sign-out action (hidden on the sign-in screen).
    property bool allowSignOut: true
    /// Show "Manage spaces" (needs the settings screen).
    property bool allowManage: true
    /// Show "Add a server or account".
    property bool allowAdd: true

    signal manageRequested()
    signal signOutRequested()

    readonly property var spaceList: typeof Workspaces !== "undefined" ? Workspaces.profiles : []
    readonly property var rows: buildRows()

    function buildRows() {
        var result = []
        for (var i = 0; i < spaceList.length; ++i)
            result.push({ type: "space", space: spaceList[i] })
        var firstAction = true
        function action(key, text, icon, destructive) {
            result.push({ type: "action", key: key, text: text, icon: icon,
                          destructive: destructive, divider: firstAction && spaceList.length > 0 })
            firstAction = false
        }
        if (allowManage)
            action("manage", qsTr("Manage spaces"), Icons.settings, false)
        if (allowAdd)
            action("add", qsTr("Add a server or account"), Icons.add, false)
        if (allowSignOut)
            action("signOut", qsTr("Sign out"), Icons.logout, true)
        return result
    }

    function currentIndexOnOpen() {
        for (var i = 0; i < spaceList.length; ++i) {
            if (spaceList[i].active)
                return i
        }
        return 0
    }

    function activate(index) {
        var row = rows[index]
        if (!row)
            return
        close()
        if (row.type === "space") {
            if (!row.space.active || !AuthenticationService.authenticated)
                Workspaces.switchProfile(row.space.id)
        } else if (row.key === "add") {
            Workspaces.beginAddConnection("")
        } else if (row.key === "manage") {
            manageRequested()
        } else if (row.key === "signOut") {
            signOutRequested()
        }
    }

    modal: true
    focus: true
    anchors.centerIn: parent
    width: Math.min(Math.round(760 * Theme.layoutScale),
                    Math.max(0, (parent ? parent.width : 0) - Theme.spacingLarge * 2))
    padding: Theme.dialogPadding
    topPadding: Theme.spacingMedium

    background: DialogSurface {}
    Overlay.modal: ModalScrim {}

    ModalFocusGuard {
        popup: root
        fallbackItem: rowList
    }

    onOpened: Qt.callLater(function() {
        rowList.currentIndex = root.currentIndexOnOpen()
        rowList.positionViewAtIndex(rowList.currentIndex, ListView.Contain)
        rowList.forceActiveFocus()
    })

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
                text: qsTr("Switch space")
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Text {
                width: parent.width
                text: qsTr("A space is one server account, or several browsed together.")
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
        }
    }

    contentItem: ListView {
        id: rowList
        implicitHeight: Math.min(contentHeight, Math.round(620 * Theme.layoutScale))
        clip: true
        spacing: Theme.spacingXSmall
        model: root.rows
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Spaces")

        Keys.onReturnPressed: root.activate(currentIndex)
        Keys.onEnterPressed: root.activate(currentIndex)
        Keys.onSpacePressed: root.activate(currentIndex)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                event.accepted = true
                root.close()
            }
        }

        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            readonly property bool isSpace: modelData.type === "space"
            readonly property bool isCurrent: isSpace && modelData.space.active
            readonly property bool focused: rowList.activeFocus && rowList.currentIndex === index
            readonly property int dividerGap: modelData.divider ? Theme.spacingMedium : 0

            width: rowList.width
            height: (isSpace ? Math.round(104 * Theme.layoutScale) : Theme.buttonHeightLarge) + dividerGap
            focusPolicy: Qt.NoFocus
            topInset: dividerGap
            topPadding: dividerGap
            leftPadding: Theme.spacingMedium
            rightPadding: Theme.spacingMedium

            Accessible.role: Accessible.ListItem
            Accessible.name: isSpace ? modelData.space.name + (isCurrent ? ", " + qsTr("current") : "")
                                     : modelData.text
            Accessible.description: isSpace ? modelData.space.detail : ""

            onClicked: root.activate(index)

            Rectangle {
                visible: row.modelData.divider === true
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: Math.round(row.dividerGap / 2)
                height: Theme.borderWidth
                color: Theme.surfaceBorder
            }

            background: Rectangle {
                id: rowBackground
                radius: Theme.radiusMedium
                color: row.focused ? Theme.surfaceInteractiveHover
                       : (row.hovered ? Theme.surfaceInteractive : "transparent")

                FocusRing {
                    inset: true
                    active: row.focused
                    radius: rowBackground.radius
                }
            }

            contentItem: RowLayout {
                spacing: Theme.spacingMedium

                SpaceAvatar {
                    visible: row.isSpace
                    name: row.isSpace ? row.modelData.space.name : ""
                    sourceCount: row.isSpace ? row.modelData.space.sourceCount : 1
                    current: row.isCurrent
                    size: Math.round(60 * Theme.layoutScale)
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    visible: !row.isSpace
                    text: row.modelData.icon || ""
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.fontSizeIcon
                    color: row.modelData.destructive ? Theme.errorColor : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    Layout.preferredWidth: Math.round(66 * Theme.layoutScale)
                    Layout.alignment: Qt.AlignVCenter
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Math.round(2 * Theme.layoutScale)

                    Text {
                        Layout.fillWidth: true
                        text: row.isSpace ? row.modelData.space.name : row.modelData.text
                        elide: Text.ElideRight
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: row.isSpace || row.focused ? Theme.fontWeightStrong : Theme.fontWeightRegular
                        color: row.modelData.destructive ? Theme.errorColor : Theme.textPrimary
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: row.isSpace && text !== ""
                        text: row.isSpace ? row.modelData.space.detail : ""
                        elide: Text.ElideRight
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                    }
                }

                RowLayout {
                    visible: row.isCurrent
                    spacing: Theme.spacingXSmall
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        text: Icons.check
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeIcon
                        color: Theme.selectionIndicator
                    }
                    Text {
                        text: qsTr("Current")
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Theme.fontWeightStrong
                        color: Theme.selectionIndicator
                    }
                }
            }
        }
    }
}
