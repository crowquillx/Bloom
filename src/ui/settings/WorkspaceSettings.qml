import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * WorkspaceSettings - manage Bloom workspaces and saved server connections.
 *
 * The page is one vertical list of rows (workspace, its sources, add-source,
 * new workspace, saved connections). Up/Down move between rows and keep the
 * button column; Left/Right move between a row's buttons; Left from the first
 * button returns to the settings rail. Focus is restored with Qt.callLater
 * after the model refreshes.
 */
FocusScope {
    id: root

    signal requestReturnToRail()

    readonly property var workspaceList: typeof Workspaces !== "undefined" ? Workspaces.profiles : []
    readonly property var connectionList: typeof Workspaces !== "undefined" ? Workspaces.connections : []
    property var rows: buildRows()
    property int preferredColumn: 0
    property string restoreRowKey: ""

    function enterFromRail() {
        if (rows.length === 0)
            return
        focusRow(Math.max(0, Math.min(rowList.currentIndex, rows.length - 1)))
    }

    function restoreFocus() {
        enterFromRail()
    }

    Keys.priority: Keys.AfterItem
    Keys.onEscapePressed: function(event) { requestReturnToRail(); event.accepted = true }

    function buildRows() {
        var result = []
        for (var i = 0; i < workspaceList.length; ++i) {
            var workspace = workspaceList[i]
            result.push({ type: "workspace", key: "w:" + workspace.id, workspace: workspace,
                          index: i, count: workspaceList.length })
            var members = workspace.members || []
            for (var j = 0; j < members.length; ++j) {
                result.push({ type: "member", key: "m:" + workspace.id + ":" + members[j].memberId,
                              workspace: workspace, member: members[j], index: j, count: members.length })
            }
            result.push({ type: "addMember", key: "a:" + workspace.id, workspace: workspace })
        }
        result.push({ type: "newWorkspace", key: "new" })
        for (var k = 0; k < connectionList.length; ++k) {
            result.push({ type: "connection", key: "c:" + connectionList[k].connectionId,
                          connection: connectionList[k], first: k === 0 })
        }
        return result
    }

    function actionsFor(row) {
        if (row.type === "workspace") {
            var w = row.workspace
            var actions = []
            if (!w.active || !AuthenticationService.authenticated)
                actions.push({ key: "switch", text: qsTr("Switch"), icon: Icons.chevronRight, variant: "primary" })
            actions.push({ key: "rename", text: qsTr("Rename"), icon: Icons.edit })
            actions.push({ key: "merge", text: w.merged ? qsTr("Merged") : qsTr("Single"),
                           icon: w.merged ? Icons.group : Icons.person, selected: w.merged,
                           label: w.merged ? qsTr("Merged: browse every source together. Select to use one source.")
                                           : qsTr("Single: one source at a time. Select to browse sources together.") })
            if (row.index > 0)
                actions.push({ key: "up", icon: Icons.expandLess, label: qsTr("Move workspace up") })
            if (row.index < row.count - 1)
                actions.push({ key: "down", icon: Icons.expandMore, label: qsTr("Move workspace down") })
            actions.push({ key: "delete", icon: Icons.delete_, variant: "destructive", label: qsTr("Delete workspace") })
            return actions
        }
        if (row.type === "member") {
            var m = row.member
            var merged = row.workspace.merged
            var memberActions = []
            if (merged)
                memberActions.push({ key: "toggle", text: m.enabled ? qsTr("On") : qsTr("Off"),
                                     icon: m.enabled ? Icons.visibility : Icons.visibilityOff, selected: m.enabled,
                                     label: m.enabled ? qsTr("Included. Select to hide this source.")
                                                      : qsTr("Hidden. Select to include this source.") })
            if (merged && !m.isDefault && m.enabled)
                memberActions.push({ key: "default", text: qsTr("Make default"), icon: Icons.starBorder })
            memberActions.push({ key: "label", icon: Icons.edit, label: qsTr("Rename source") })
            if (merged && row.index > 0)
                memberActions.push({ key: "up", icon: Icons.expandLess, label: qsTr("Move source up") })
            if (merged && row.index < row.count - 1)
                memberActions.push({ key: "down", icon: Icons.expandMore, label: qsTr("Move source down") })
            if (row.count > 1)
                memberActions.push({ key: "remove", icon: Icons.close, variant: "destructive",
                                     label: qsTr("Remove source from workspace") })
            return memberActions
        }
        if (row.type === "addMember")
            return [{ key: "add", text: qsTr("Add server or account"), icon: Icons.add }]
        if (row.type === "newWorkspace")
            return [{ key: "new", text: qsTr("New workspace"), icon: Icons.add, variant: "primary" }]
        if (row.type === "connection")
            return [{ key: "forget", text: qsTr("Remove"), icon: Icons.delete_, variant: "destructive",
                      label: qsTr("Remove %1 and its saved sign-in").arg(row.connection.label) }]
        return []
    }

    function rowIndexForKey(key) {
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].key === key)
                return i
        }
        return -1
    }

    function focusRow(index) {
        if (index < 0 || index >= rows.length)
            return
        rowList.currentIndex = index
        rowList.positionViewAtIndex(index, ListView.Contain)
        Qt.callLater(function() {
            var item = rowList.itemAtIndex(index)
            if (item)
                item["focusButton"](root.preferredColumn)
        })
    }

    function moveRow(delta) {
        focusRow(Math.max(0, Math.min(rows.length - 1, rowList.currentIndex + delta)))
    }

    // Model updates rebuild every delegate; return to the same logical row.
    onRowsChanged: {
        if (restoreRowKey === "")
            return
        var index = rowIndexForKey(restoreRowKey)
        if (index < 0)
            index = Math.min(rowList.currentIndex, rows.length - 1)
        if (root.activeFocus)
            focusRow(index)
        else
            rowList.currentIndex = index
    }

    function rememberRow(row) {
        restoreRowKey = row.key
    }

    function trigger(row, key) {
        rememberRow(row)
        var w = row.workspace
        switch (row.type + ":" + key) {
        case "workspace:switch":
            Workspaces.switchProfile(w.id)
            break
        case "workspace:rename":
            openTextDialog(qsTr("Rename workspace"), qsTr("Workspace name"), w.name, false,
                           function(text) { Workspaces.renameProfile(w.id, text) })
            break
        case "workspace:merge":
            if (w.merged && (w.members || []).length > 1) {
                openConfirm(qsTr("Use one source?"),
                            qsTr("Single workspaces use only their default source. The other sources are removed from \"%1\"; their saved sign-ins are kept.").arg(w.name),
                            qsTr("Use one source"),
                            function() { Workspaces.setProfileMerged(w.id, false) })
            } else {
                Workspaces.setProfileMerged(w.id, !w.merged)
            }
            break
        case "workspace:up":
            Workspaces.moveProfile(w.id, -1)
            break
        case "workspace:down":
            Workspaces.moveProfile(w.id, 1)
            break
        case "workspace:delete":
            openConfirm(qsTr("Delete workspace?"),
                        qsTr("\"%1\" is removed. Saved sign-ins and other workspaces are not affected.").arg(w.name),
                        qsTr("Delete"),
                        function() { Workspaces.removeProfile(w.id) })
            break
        case "member:toggle":
            if (!Workspaces.setMemberEnabled(w.id, row.member.memberId, !row.member.enabled))
                toast.show(qsTr("A workspace needs at least one source."))
            break
        case "member:default":
            Workspaces.setDefaultMember(w.id, row.member.memberId)
            break
        case "member:label":
            openTextDialog(qsTr("Rename source"), qsTr("Leave empty to use \"%1\"").arg(row.member.accountLabel + " · " + row.member.serverLabel),
                           row.member.labelOverride, true,
                           function(text) { Workspaces.setMemberLabel(w.id, row.member.memberId, text) })
            break
        case "member:up":
            Workspaces.moveMember(w.id, row.member.memberId, -1)
            break
        case "member:down":
            Workspaces.moveMember(w.id, row.member.memberId, 1)
            break
        case "member:remove":
            Workspaces.removeMember(w.id, row.member.memberId)
            break
        case "addMember:add":
            openConnectionChoice(qsTr("Add to \"%1\"").arg(w.name), w, function(connectionId) {
                if (connectionId === "")
                    Workspaces.beginAddConnection(w.id)
                else
                    Workspaces.addMember(w.id, connectionId)
            })
            break
        case "newWorkspace:new":
            openConnectionChoice(qsTr("New workspace"), null, function(connectionId) {
                if (connectionId === "")
                    Workspaces.beginAddConnection("")
                else
                    Workspaces.createProfile("", [connectionId], false)
            })
            break
        case "connection:forget":
            openConfirm(qsTr("Remove %1?").arg(row.connection.label),
                        row.connection.live
                        ? qsTr("You are signed in with this account. Bloom signs it out, deletes its saved sign-in, and removes it from every workspace.")
                        : qsTr("Bloom deletes its saved sign-in and removes it from every workspace. Other accounts are not affected."),
                        qsTr("Remove"),
                        function() { Workspaces.removeConnection(row.connection.connectionId) })
            break
        }
    }

    // ---- Dialog helpers -------------------------------------------------

    property var pendingCallback: null

    function openConfirm(title, message, confirmText, callback) {
        pendingCallback = callback
        confirmDialog.titleText = title
        confirmDialog.messageText = message
        confirmDialog.confirmText = confirmText
        confirmDialog.open()
    }

    function openTextDialog(title, placeholder, value, allowEmpty, callback) {
        pendingCallback = callback
        textDialog.titleText = title
        textDialog.placeholder = placeholder
        textDialog.initialText = value || ""
        textDialog.allowEmpty = allowEmpty
        textDialog.open()
    }

    function openConnectionChoice(title, workspace, callback) {
        var choices = []
        var existing = {}
        var members = workspace ? (workspace.members || []) : []
        for (var i = 0; i < members.length; ++i)
            existing[members[i].connectionId] = true
        for (var j = 0; j < connectionList.length; ++j) {
            var connection = connectionList[j]
            if (!existing[connection.connectionId])
                choices.push({ key: connection.connectionId, label: connection.label,
                               detail: connection.providerName + " · " + connection.baseUrl })
        }
        choices.push({ key: "", label: qsTr("Sign in to another server or account…"),
                       detail: qsTr("Opens the sign-in screen; your current sign-in is kept.") })
        pendingCallback = callback
        choiceDialog.titleText = title
        choiceDialog.choices = choices
        choiceDialog.open()
    }

    function finishDialog(value) {
        var callback = pendingCallback
        pendingCallback = null
        if (callback)
            callback(value)
        Qt.callLater(function() {
            var index = root.rowIndexForKey(root.restoreRowKey)
            root.focusRow(index >= 0 ? index : Math.min(rowList.currentIndex, root.rows.length - 1))
        })
    }

    function cancelDialog() {
        pendingCallback = null
        Qt.callLater(function() { root.focusRow(rowList.currentIndex) })
    }

    // ---- Layout ---------------------------------------------------------

    GlassPanel {
        anchors.fill: parent
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.panelPadding - Theme.spacingMedium
        spacing: 0

        Text {
            text: qsTr("Workspaces")
            font.pixelSize: Theme.fontSizeHeader
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightHeavy
            color: Theme.textPrimary
            Layout.leftMargin: Theme.spacingMedium
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Text {
            text: qsTr("A workspace groups servers and accounts. Single workspaces show one source; merged workspaces browse every included source together and label where each title comes from. Two users on the same server can be separate sources.")
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingLarge
        }

        ListView {
            id: rowList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            spacing: Theme.spacingXSmall
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            highlightFollowsCurrentItem: false
            focus: true

            delegate: FocusScope {
                id: rowItem
                required property var modelData
                required property int index
                readonly property var actions: root.actionsFor(modelData)
                readonly property bool isHeaderRow: modelData.type === "workspace"
                readonly property bool isMemberRow: modelData.type === "member"
                width: rowList.width
                height: Math.round((modelData.type === "connection" && modelData.first ? 132 : 92) * Theme.layoutScale)

                function focusButton(column) {
                    if (buttonRepeater.count === 0)
                        return
                    var target = buttonRepeater.itemAt(Math.max(0, Math.min(column, buttonRepeater.count - 1)))
                    if (target)
                        target.forceActiveFocus()
                }

                Text {
                    id: connectionsHeading
                    visible: rowItem.modelData.type === "connection" && rowItem.modelData.first
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingSmall
                    text: qsTr("Saved connections")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightStrong
                    color: Theme.textPrimary
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                Rectangle {
                    id: rowBackground
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: rowItem.isMemberRow || rowItem.modelData.type === "addMember"
                                        ? Math.round(48 * Theme.layoutScale) : 0
                    height: Math.round(88 * Theme.layoutScale)
                    radius: Theme.radiusMedium
                    color: rowItem.activeFocus ? Theme.surfaceInteractive
                           : (rowItem.isHeaderRow && rowItem.modelData.workspace.active
                              ? Theme.selectionBackground : "transparent")

                    Rectangle {
                        visible: rowItem.isHeaderRow && rowItem.modelData.workspace.active
                        width: Theme.selectionIndicatorWidth
                        height: Math.round(parent.height * 0.44)
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(6 * Theme.layoutScale)
                        anchors.verticalCenter: parent.verticalCenter
                        radius: width / 2
                        color: Theme.selectionIndicator
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium + Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingSmall
                        spacing: Theme.spacingSmall

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Math.round(2 * Theme.layoutScale)
                            visible: rowItem.modelData.type !== "addMember" && rowItem.modelData.type !== "newWorkspace"

                            RowLayout {
                                spacing: Theme.spacingSmall
                                Layout.fillWidth: true

                                Text {
                                    Layout.fillWidth: true
                                    Layout.maximumWidth: implicitWidth
                                    text: rowItem.isHeaderRow ? rowItem.modelData.workspace.name
                                          : (rowItem.isMemberRow ? rowItem.modelData.member.label
                                             : (rowItem.modelData.connection ? rowItem.modelData.connection.label : ""))
                                    elide: Text.ElideRight
                                    font.pixelSize: rowItem.isHeaderRow ? Theme.fontSizeBody : Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    font.weight: rowItem.isHeaderRow ? Theme.fontWeightStrong : Theme.fontWeightRegular
                                    color: rowItem.isMemberRow && !rowItem.modelData.member.enabled
                                           ? Theme.textTertiary : Theme.textPrimary
                                }

                                MetadataChip {
                                    text: rowItem.isHeaderRow && rowItem.modelData.workspace.active ? qsTr("Current") : ""
                                }
                                MetadataChip {
                                    text: rowItem.isMemberRow && rowItem.modelData.member.isDefault
                                          && rowItem.modelData.workspace.merged ? qsTr("Default") : ""
                                }
                                MetadataChip {
                                    text: (rowItem.isMemberRow || rowItem.modelData.type === "connection")
                                          && (rowItem.isMemberRow ? rowItem.modelData.member.live
                                                                  : rowItem.modelData.connection.live)
                                          ? qsTr("Signed in") : ""
                                }
                                Item { Layout.fillWidth: true }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: rowItem.isHeaderRow
                                      ? (rowItem.modelData.workspace.merged
                                         ? qsTr("Merged · %1").arg(rowItem.modelData.workspace.summary)
                                         : rowItem.modelData.workspace.summary)
                                      : (rowItem.isMemberRow
                                         ? rowItem.modelData.member.providerName + " · " + rowItem.modelData.member.baseUrl
                                         : (rowItem.modelData.connection
                                            ? rowItem.modelData.connection.providerName + " · " + rowItem.modelData.connection.baseUrl
                                            : ""))
                                elide: Text.ElideRight
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                            }
                        }

                        Repeater {
                            id: buttonRepeater
                            model: rowItem.actions

                            ActionButton {
                                id: rowButton
                                required property var modelData
                                required property int index
                                Layout.fillWidth: rowItem.modelData.type === "addMember"
                                                  || rowItem.modelData.type === "newWorkspace"
                                Layout.preferredHeight: Theme.buttonHeightMedium
                                variant: modelData.variant || "secondary"
                                selected: !!modelData.selected
                                iconGlyph: modelData.icon || ""
                                text: modelData.text || ""
                                accessibleLabel: modelData.label || modelData.text || ""
                                toolTipText: modelData.label || ""
                                focusPolicy: Qt.StrongFocus

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        root.preferredColumn = index
                                        rowList.currentIndex = rowItem.index
                                    }
                                }
                                onClicked: root.trigger(rowItem.modelData, modelData.key)
                                Keys.onReturnPressed: root.trigger(rowItem.modelData, modelData.key)
                                Keys.onEnterPressed: root.trigger(rowItem.modelData, modelData.key)
                                Keys.onLeftPressed: function(event) {
                                    if (index > 0)
                                        buttonRepeater.itemAt(index - 1).forceActiveFocus()
                                    else
                                        root.requestReturnToRail()
                                    event.accepted = true
                                }
                                Keys.onRightPressed: function(event) {
                                    if (index < buttonRepeater.count - 1)
                                        buttonRepeater.itemAt(index + 1).forceActiveFocus()
                                    event.accepted = true
                                }
                                Keys.onUpPressed: function(event) { root.moveRow(-1); event.accepted = true }
                                Keys.onDownPressed: function(event) { root.moveRow(1); event.accepted = true }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- Dialogs --------------------------------------------------------

    Dialog {
        id: confirmDialog
        property string titleText: ""
        property string messageText: ""
        property string confirmText: ""
        property bool confirmed: false
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(Math.round(620 * Theme.layoutScale), parent ? parent.width - Theme.spacingLarge * 2 : 600)
        padding: Theme.dialogPadding
        background: DialogSurface {}
        Overlay.modal: ModalScrim {}

        onOpened: {
            confirmed = false
            Qt.callLater(function() { confirmCancel.forceActiveFocus() })
        }
        onClosed: confirmed ? root.finishDialog(true) : root.cancelDialog()

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                text: confirmDialog.titleText
                font.pixelSize: Theme.fontSizeTitle
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Text {
                text: confirmDialog.messageText
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.controlSpacing
                Keys.onEscapePressed: confirmDialog.close()

                ActionButton {
                    id: confirmCancel
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    KeyNavigation.right: confirmAccept
                    Keys.onReturnPressed: confirmDialog.close()
                    Keys.onEnterPressed: confirmDialog.close()
                    onClicked: confirmDialog.close()
                }
                ActionButton {
                    id: confirmAccept
                    Layout.fillWidth: true
                    variant: "destructive"
                    text: confirmDialog.confirmText
                    KeyNavigation.left: confirmCancel
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    onClicked: {
                        confirmDialog.confirmed = true
                        confirmDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: textDialog
        property string titleText: ""
        property string placeholder: ""
        property string initialText: ""
        property bool allowEmpty: false
        property bool accepted_: false
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(Math.round(620 * Theme.layoutScale), parent ? parent.width - Theme.spacingLarge * 2 : 600)
        padding: Theme.dialogPadding
        background: DialogSurface {}
        Overlay.modal: ModalScrim {}

        function submit() {
            if (!textDialog.allowEmpty && textField.text.trim() === "")
                return
            textDialog.accepted_ = true
            textDialog.close()
        }

        onOpened: {
            accepted_ = false
            textField.text = initialText
            Qt.callLater(function() {
                textField.forceActiveFocus()
                textField.selectAll()
            })
        }
        onClosed: accepted_ ? root.finishDialog(textField.text.trim()) : root.cancelDialog()

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                text: textDialog.titleText
                font.pixelSize: Theme.fontSizeTitle
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
            }
            TextField {
                id: textField
                Layout.fillWidth: true
                placeholderText: textDialog.placeholder
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                leftPadding: Theme.spacingMedium
                rightPadding: Theme.spacingMedium
                background: FieldBackground { control: textField; implicitHeight: Theme.buttonHeightSmall }
                Keys.onDownPressed: function(event) { textSave.forceActiveFocus(); event.accepted = true }
                Keys.onEscapePressed: textDialog.close()
                onAccepted: textDialog.submit()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.controlSpacing
                Keys.onEscapePressed: textDialog.close()

                ActionButton {
                    id: textCancel
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    KeyNavigation.right: textSave
                    KeyNavigation.up: textField
                    Keys.onReturnPressed: textDialog.close()
                    Keys.onEnterPressed: textDialog.close()
                    onClicked: textDialog.close()
                }
                ActionButton {
                    id: textSave
                    Layout.fillWidth: true
                    variant: "primary"
                    text: qsTr("Save")
                    enabled: textDialog.allowEmpty || textField.text.trim() !== ""
                    KeyNavigation.left: textCancel
                    KeyNavigation.up: textField
                    Keys.onReturnPressed: textDialog.submit()
                    Keys.onEnterPressed: textDialog.submit()
                    onClicked: textDialog.submit()
                }
            }
        }
    }

    Dialog {
        id: choiceDialog
        property string titleText: ""
        property var choices: []
        property string chosenKey: ""
        property bool chose: false
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(Math.round(720 * Theme.layoutScale), parent ? parent.width - Theme.spacingLarge * 2 : 700)
        padding: Theme.dialogPadding
        background: DialogSurface {}
        Overlay.modal: ModalScrim {}

        function choose(index) {
            var choice = choices[index]
            if (!choice)
                return
            chosenKey = choice.key
            chose = true
            close()
        }

        onOpened: {
            chose = false
            choiceList.currentIndex = 0
            Qt.callLater(function() { choiceList.forceActiveFocus() })
        }
        onClosed: chose ? root.finishDialog(chosenKey) : root.cancelDialog()

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                text: choiceDialog.titleText
                font.pixelSize: Theme.fontSizeTitle
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            ListView {
                id: choiceList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Math.round(480 * Theme.layoutScale))
                clip: true
                model: choiceDialog.choices
                spacing: Theme.spacingXSmall
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                Keys.onReturnPressed: choiceDialog.choose(currentIndex)
                Keys.onEnterPressed: choiceDialog.choose(currentIndex)
                Keys.onEscapePressed: choiceDialog.close()

                delegate: ItemDelegate {
                    id: choiceRow
                    required property var modelData
                    required property int index
                    width: choiceList.width
                    height: Math.round(84 * Theme.layoutScale)
                    focusPolicy: Qt.NoFocus
                    readonly property bool focused: choiceList.activeFocus && choiceList.currentIndex === index
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.label
                    Accessible.description: modelData.detail
                    onClicked: choiceDialog.choose(index)

                    background: Rectangle {
                        id: choiceBackground
                        radius: Theme.radiusMedium
                        color: choiceRow.focused ? Theme.surfaceInteractiveHover
                               : (choiceRow.hovered ? Theme.surfaceInteractive : "transparent")
                        FocusRing {
                            inset: true
                            active: choiceRow.focused
                            radius: choiceBackground.radius
                        }
                    }

                    contentItem: ColumnLayout {
                        spacing: Math.round(2 * Theme.layoutScale)
                        Text {
                            Layout.fillWidth: true
                            text: choiceRow.modelData.label
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            font.weight: Theme.fontWeightStrong
                            color: Theme.textPrimary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: choiceRow.modelData.detail
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                        }
                    }
                }
            }
        }
    }

    ToastNotification {
        id: toast
        z: 10
    }
}
