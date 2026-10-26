import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * SpaceSettings - manage Bloom spaces and saved server accounts.
 *
 * Two levels share one row list. The overview lists spaces, "New space", and
 * saved accounts; Enter on a space opens its detail: an action row (Switch,
 * Rename, Move, Delete), the space's sources, and "Add a server or account".
 * Enter on a source opens its options menu. Up/Down move between rows,
 * Left/Right move within the action row, Left from a row's first control
 * returns to the settings rail, and Escape/Back goes up one level. Focus is
 * restored with Qt.callLater after the model refreshes.
 */
FocusScope {
    id: root

    signal requestReturnToRail()
    /// Scrolled into the rows; Settings recedes the navigation bar.
    readonly property bool scrolled: rowList.contentY - rowList.originY > Theme.spacingMedium

    readonly property var spaceList: typeof Workspaces !== "undefined" ? Workspaces.profiles : []
    readonly property var connectionList: typeof Workspaces !== "undefined" ? Workspaces.connections : []
    /// Space shown in detail; empty for the overview.
    property string openSpaceId: ""
    readonly property var openSpace: findSpace(openSpaceId)
    readonly property int openSpaceIndex: spaceIndex(openSpaceId)
    property var rows: buildRows()
    property int preferredColumn: 0
    property string restoreRowKey: ""
    property string restoreActionKey: ""

    function enterFromRail() {
        if (rows.length === 0)
            return
        var index = rowIndexForKey(restoreRowKey)
        focusRow(index >= 0 ? index : Math.max(0, Math.min(rowList.currentIndex, rows.length - 1)))
    }

    function restoreFocus() {
        enterFromRail()
    }

    Keys.priority: Keys.AfterItem
    // In a space's detail, Escape/Back goes up to the list instead of
    // triggering Main's Back shortcut, which would leave Settings.
    Keys.onShortcutOverride: function(event) {
        if (openSpaceId !== "" && (event.key === Qt.Key_Escape || event.key === Qt.Key_Back))
            event.accepted = true
    }
    Keys.onPressed: function(event) {
        if (event.key !== Qt.Key_Escape && event.key !== Qt.Key_Back)
            return
        event.accepted = true
        if (openSpaceId !== "")
            closeSpace()
        else
            requestReturnToRail()
    }

    function findSpace(id) {
        var index = spaceIndex(id)
        return index >= 0 ? spaceList[index] : null
    }

    function spaceIndex(id) {
        for (var i = 0; i < spaceList.length; ++i) {
            if (spaceList[i].id === id)
                return i
        }
        return -1
    }

    // A deleted space closes its detail.
    onOpenSpaceChanged: {
        if (openSpaceId !== "" && !openSpace)
            closeSpace()
    }

    function openSpaceDetail(id) {
        restoreRowKey = "actions"
        restoreActionKey = ""
        preferredColumn = 0
        openSpaceId = id
        Qt.callLater(function() { root.focusRow(0) })
    }

    function closeSpace() {
        var key = "s:" + openSpaceId
        openSpaceId = ""
        restoreRowKey = key
        Qt.callLater(function() {
            var index = root.rowIndexForKey(key)
            root.focusRow(index >= 0 ? index : 0)
        })
    }

    function buildRows() {
        var result = []
        var space = openSpace
        if (space) {
            result.push({ type: "actions", key: "actions", space: space })
            var members = space.members || []
            for (var j = 0; j < members.length; ++j) {
                result.push({ type: "member", key: "m:" + members[j].memberId, space: space,
                              member: members[j], index: j, count: members.length,
                              heading: j === 0 ? qsTr("Sources") : "" })
            }
            result.push({ type: "addMember", key: "add", space: space })
            return result
        }
        for (var i = 0; i < spaceList.length; ++i)
            result.push({ type: "space", key: "s:" + spaceList[i].id, space: spaceList[i] })
        result.push({ type: "newSpace", key: "new" })
        for (var k = 0; k < connectionList.length; ++k) {
            result.push({ type: "connection", key: "c:" + connectionList[k].connectionId,
                          connection: connectionList[k],
                          heading: k === 0 ? qsTr("Saved accounts") : "" })
        }
        return result
    }

    function spaceActions(space) {
        var actions = []
        if (!space.active || !AuthenticationService.authenticated)
            actions.push({ key: "switch", text: qsTr("Switch to space"), icon: Icons.arrowForward, variant: "primary" })
        actions.push({ key: "rename", text: qsTr("Rename"), icon: Icons.edit })
        if (openSpaceIndex > 0)
            actions.push({ key: "up", text: qsTr("Move up"), icon: Icons.expandLess })
        if (openSpaceIndex >= 0 && openSpaceIndex < spaceList.length - 1)
            actions.push({ key: "down", text: qsTr("Move down"), icon: Icons.expandMore })
        actions.push({ key: "delete", text: qsTr("Delete"), icon: Icons.delete_, variant: "destructive" })
        return actions
    }

    function memberActions(row) {
        var m = row.member
        var several = row.count > 1
        var actions = []
        if (several) {
            actions.push(m.enabled
                         ? { key: "toggle", text: qsTr("Hide from this space"), icon: Icons.visibilityOff }
                         : { key: "toggle", text: qsTr("Show in this space"), icon: Icons.visibility })
        }
        if (several && m.enabled && !m.isDefault)
            actions.push({ key: "default", text: qsTr("Make primary"), icon: Icons.starBorder })
        actions.push({ key: "libraries", text: qsTr("Choose libraries"), icon: Icons.videoLibrary })
        actions.push({ key: "label", text: qsTr("Rename"), icon: Icons.edit })
        if (several && row.index > 0)
            actions.push({ key: "up", text: qsTr("Move up"), icon: Icons.expandLess })
        if (several && row.index < row.count - 1)
            actions.push({ key: "down", text: qsTr("Move down"), icon: Icons.expandMore })
        if (several)
            actions.push({ key: "remove", text: qsTr("Remove from space"), icon: Icons.close, destructive: true })
        return actions
    }

    // "Jellyfin · https://host · 2 libraries hidden · 1 not searched"
    function memberDetail(member) {
        var parts = [member.providerName + " · " + member.baseUrl]
        var hidden = (member.hiddenLibraryIds || []).length
        var unsearched = (member.searchHiddenLibraryIds || []).length
        if (hidden > 0)
            parts.push(qsTr("%n libraries hidden", "", hidden))
        if (unsearched > 0)
            parts.push(qsTr("%n not searched", "", unsearched))
        return parts.join(" · ")
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
        scrollRowIntoView(index)
        Qt.callLater(function() {
            var item = rowList.itemAtIndex(index)
            if (item)
                item["focusIn"]()
        })
    }

    // Keep the row and some context in view, animated like the other settings
    // pages (instant under Reduce Motion).
    function scrollRowIntoView(index) {
        var item = rowList.itemAtIndex(index)
        if (!item) {
            rowList.positionViewAtIndex(index, ListView.Contain)
            return
        }
        var margin = Theme.navBarItemHeight
        var current = rowScroll.running ? rowScroll.to : rowList.contentY
        var target = current
        if (item.y - margin < current)
            target = item.y - margin
        else if (item.y + item.height + margin > current + rowList.height)
            target = item.y + item.height + margin - rowList.height
        // The list's own margins are part of the scrollable range.
        var minY = rowList.originY - rowList.topMargin
        var maxY = Math.max(minY, rowList.originY + rowList.contentHeight
                                  + rowList.bottomMargin - rowList.height)
        target = Math.max(minY, Math.min(maxY, target))
        if (Math.abs(target - rowList.contentY) < 1)
            return
        rowScroll.stop()
        rowScroll.from = rowList.contentY
        rowScroll.to = target
        rowScroll.start()
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

    function activateRow(row) {
        restoreRowKey = row.key
        switch (row.type) {
        case "space":
            openSpaceDetail(row.space.id)
            break
        case "member":
            memberMenu.openFor(row)
            break
        case "addMember":
            openConnectionChoice(qsTr("Add to \"%1\"").arg(row.space.name), row.space, function(connectionId) {
                if (connectionId === "")
                    Workspaces.beginAddConnection(row.space.id)
                else
                    Workspaces.addMember(row.space.id, connectionId)
            })
            break
        case "newSpace":
            openConnectionChoice(qsTr("New space"), null, function(connectionId) {
                if (connectionId === "")
                    Workspaces.beginAddConnection("")
                else
                    Workspaces.createProfile("", [connectionId], false)
            })
            break
        case "connection":
            openConfirm(qsTr("Remove %1?").arg(row.connection.label),
                        row.connection.live
                        ? qsTr("You are signed in with this account. Bloom signs it out, deletes its saved sign-in, and removes it from every space.")
                        : qsTr("Bloom deletes its saved sign-in and removes it from every space. Other accounts are not affected."),
                        qsTr("Remove"),
                        function() { Workspaces.removeConnection(row.connection.connectionId) })
            break
        }
    }

    function triggerSpaceAction(space, key) {
        restoreRowKey = "actions"
        restoreActionKey = key
        switch (key) {
        case "switch":
            Workspaces.switchProfile(space.id)
            break
        case "rename":
            openTextDialog(qsTr("Rename space"), qsTr("Space name"), space.name, false,
                           function(text) { Workspaces.renameProfile(space.id, text) })
            break
        case "up":
            Workspaces.moveProfile(space.id, -1)
            break
        case "down":
            Workspaces.moveProfile(space.id, 1)
            break
        case "delete":
            openConfirm(qsTr("Delete \"%1\"?").arg(space.name),
                        qsTr("Only the space is deleted. Its accounts stay signed in and other spaces are not affected."),
                        qsTr("Delete"),
                        function() { Workspaces.removeProfile(space.id) })
            break
        }
    }

    function triggerMemberAction(row, key) {
        restoreRowKey = row.key
        var space = row.space
        var member = row.member
        switch (key) {
        case "toggle":
            if (!Workspaces.setMemberEnabled(space.id, member.memberId, !member.enabled))
                toast.show(qsTr("A space needs at least one shown source."))
            break
        case "default":
            Workspaces.setDefaultMember(space.id, member.memberId)
            break
        case "libraries":
            librariesDialog.openFor(space, member)
            break
        case "label":
            openTextDialog(qsTr("Rename source"),
                           qsTr("Leave empty to use \"%1\"").arg(member.accountLabel + " · " + member.serverLabel),
                           member.labelOverride, true,
                           function(text) { Workspaces.setMemberLabel(space.id, member.memberId, text) })
            break
        case "up":
            Workspaces.moveMember(space.id, member.memberId, -1)
            break
        case "down":
            Workspaces.moveMember(space.id, member.memberId, 1)
            break
        case "remove":
            openConfirm(qsTr("Remove %1 from \"%2\"?").arg(member.label).arg(space.name),
                        qsTr("The account stays signed in and keeps its place in other spaces."),
                        qsTr("Remove"),
                        function() { Workspaces.removeMember(space.id, member.memberId) })
            break
        }
        if (key !== "label" && key !== "remove" && key !== "libraries")
            Qt.callLater(function() { root.focusRow(root.rowIndexForKey(root.restoreRowKey)) })
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

    function openConnectionChoice(title, space, callback) {
        var choices = []
        var existing = {}
        var members = space ? (space.members || []) : []
        for (var i = 0; i < members.length; ++i)
            existing[members[i].connectionId] = true
        for (var j = 0; j < connectionList.length; ++j) {
            var connection = connectionList[j]
            if (!existing[connection.connectionId])
                choices.push({ key: connection.connectionId, label: connection.label,
                               detail: connection.providerName + " · " + connection.baseUrl })
        }
        choices.push({ key: "", label: qsTr("Sign in to another server or account…"),
                       detail: qsTr("Opens the sign-in screen. Your current sign-in is kept.") })
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

    NumberAnimation {
        id: rowScroll
        target: rowList
        property: "contentY"
        duration: Theme.durationNormal
        easing.type: Theme.easingStandard
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Overview header
        ColumnLayout {
            visible: !root.openSpace
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            Layout.bottomMargin: Theme.spacingLarge
            spacing: Theme.spacingSmall

            Text {
                text: qsTr("A space is what Bloom shows you: one server account, or several browsed together with each title labeled by where it comes from. Two users on the same server can be separate sources.")
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // Detail header
        ColumnLayout {
            visible: !!root.openSpace
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            Layout.bottomMargin: Theme.spacingMedium
            spacing: Theme.spacingMedium

            // Pointer path back; keys use Escape/Back.
            Row {
                id: backRow
                spacing: Theme.spacingXSmall
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Back to spaces")

                Text {
                    text: Icons.chevronLeft
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.iconSizeMedium
                    color: backLabel.color
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    id: backLabel
                    text: qsTr("Spaces")
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightStrong
                    color: backHover.hovered ? Theme.textPrimary : Theme.textSecondary
                    anchors.verticalCenter: parent.verticalCenter
                }

                HoverHandler {
                    id: backHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: root.closeSpace()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                SpaceAvatar {
                    name: root.openSpace ? root.openSpace.name : ""
                    sourceCount: root.openSpace ? root.openSpace.sourceCount : 1
                    current: !!root.openSpace && root.openSpace.active
                    size: Math.round(72 * Theme.layoutScale)
                    Layout.alignment: Qt.AlignVCenter
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Math.round(2 * Theme.layoutScale)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            Layout.maximumWidth: Math.ceil(implicitWidth)
                            Layout.fillWidth: true
                            text: root.openSpace ? root.openSpace.name : ""
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSizeHeader
                            font.family: Theme.fontPrimary
                            font.weight: Theme.fontWeightHeavy
                            color: Theme.textPrimary
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }
                        MetadataChip {
                            text: root.openSpace && root.openSpace.active ? qsTr("Current") : ""
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: !root.openSpace ? ""
                              : (root.openSpace.sourceCount > 1
                                 ? qsTr("Titles from every shown source appear together. Bloom signs in with the primary source when you switch to this space.")
                                 : qsTr("Add another server or account to browse them together."))
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                    }
                }
            }
        }

        ListView {
            id: rowList
            Layout.fillWidth: true
            Layout.fillHeight: true
            bottomMargin: Theme.pageMarginBottom
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
                readonly property string rowType: modelData.type
                readonly property bool isActions: rowType === "actions"
                readonly property bool isSpace: rowType === "space"
                readonly property bool isMember: rowType === "member"
                readonly property bool isConnection: rowType === "connection"
                readonly property bool isAdd: rowType === "newSpace" || rowType === "addMember"
                readonly property var actions: isActions ? root.spaceActions(modelData.space) : []
                readonly property string heading: modelData.heading || ""
                readonly property int headingHeight: heading !== "" ? Math.round(64 * Theme.layoutScale) : 0
                readonly property int bodyHeight: isActions ? Theme.buttonHeightMedium + Theme.spacingMedium
                                                  : (isSpace ? Math.round(104 * Theme.layoutScale)
                                                     : (isAdd ? Math.round(76 * Theme.layoutScale)
                                                        : Math.round(88 * Theme.layoutScale)))
                // A source keeps its highlight while its options menu is open.
                readonly property bool rowFocused: !isActions
                                                   && (activeFocus || (memberMenu.opened && !!memberMenu.row
                                                                       && memberMenu.row.key === modelData.key))

                width: rowList.width
                height: headingHeight + bodyHeight

                Accessible.role: isActions ? Accessible.Grouping : Accessible.ListItem
                Accessible.name: isSpace ? modelData.space.name
                                 : (isMember ? modelData.member.label
                                    : (isConnection ? modelData.connection.label : titleText.text))
                Accessible.description: detailText.text

                function focusIn() {
                    if (!isActions) {
                        rowItem.forceActiveFocus()
                        return
                    }
                    var target = -1
                    for (var i = 0; i < actions.length; ++i) {
                        if (actions[i].key === root.restoreActionKey)
                            target = i
                    }
                    if (target < 0)
                        target = Math.max(0, Math.min(root.preferredColumn, buttonRepeater.count - 1))
                    var button = buttonRepeater.itemAt(target)
                    if (button)
                        button.forceActiveFocus()
                }

                Keys.onUpPressed: function(event) {
                    // The first row leaves Up to Main, which focuses the navigation bar.
                    if (index === 0) {
                        event.accepted = false
                        return
                    }
                    root.moveRow(-1)
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) { root.moveRow(1); event.accepted = true }
                Keys.onLeftPressed: function(event) { root.requestReturnToRail(); event.accepted = true }
                Keys.onReturnPressed: function(event) { root.activateRow(modelData); event.accepted = true }
                Keys.onEnterPressed: function(event) { root.activateRow(modelData); event.accepted = true }
                onActiveFocusChanged: {
                    if (activeFocus)
                        rowList.currentIndex = index
                }

                Text {
                    visible: rowItem.heading !== ""
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.bottom: body.top
                    anchors.bottomMargin: Theme.spacingSmall
                    text: rowItem.heading
                    font.pixelSize: Theme.fontSizeMedium
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightStrong
                    color: Theme.textPrimary
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                Rectangle {
                    id: body
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: rowItem.bodyHeight
                    radius: Theme.radiusMedium
                    color: rowItem.rowFocused ? Theme.surfaceInteractiveHover
                           : (rowMouse.containsMouse ? Theme.surfaceInteractive : "transparent")

                    FocusRing {
                        inset: true
                        active: rowItem.rowFocused
                        radius: body.radius
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        enabled: !rowItem.isActions
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            rowItem.forceActiveFocus()
                            root.activateRow(rowItem.modelData)
                        }
                    }

                    // Space, source, account, and add rows
                    RowLayout {
                        visible: !rowItem.isActions
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingMedium

                        SpaceAvatar {
                            visible: rowItem.isSpace
                            name: rowItem.isSpace ? rowItem.modelData.space.name : ""
                            sourceCount: rowItem.isSpace ? rowItem.modelData.space.sourceCount : 1
                            current: rowItem.isSpace && rowItem.modelData.space.active
                            size: Math.round(60 * Theme.layoutScale)
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Text {
                            visible: rowItem.isAdd
                            text: Icons.add
                            font.family: Theme.fontIcon
                            font.pixelSize: Theme.fontSizeIcon
                            color: Theme.accentPrimary
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: Math.round(2 * Theme.layoutScale)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                Text {
                                    id: titleText
                                    // Rounded up: layouts truncate fractional widths, which
                                    // would elide a title that fits.
                                    Layout.maximumWidth: Math.ceil(implicitWidth)
                                    Layout.fillWidth: true
                                    text: rowItem.isSpace ? rowItem.modelData.space.name
                                          : rowItem.isMember ? rowItem.modelData.member.label
                                          : rowItem.isConnection ? rowItem.modelData.connection.label
                                          : rowItem.rowType === "newSpace" ? qsTr("New space")
                                          : qsTr("Add a server or account")
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    font.weight: Theme.fontWeightStrong
                                    color: rowItem.isMember && !rowItem.modelData.member.enabled
                                           ? Theme.textTertiary : Theme.textPrimary
                                }

                                MetadataChip {
                                    text: rowItem.isSpace && rowItem.modelData.space.active ? qsTr("Current") : ""
                                }
                                MetadataChip {
                                    text: rowItem.isMember && rowItem.modelData.count > 1
                                          && rowItem.modelData.member.isDefault ? qsTr("Primary") : ""
                                }
                                MetadataChip {
                                    text: rowItem.isMember && !rowItem.modelData.member.enabled ? qsTr("Hidden") : ""
                                }
                                MetadataChip {
                                    text: (rowItem.isMember && rowItem.modelData.member.live)
                                          || (rowItem.isConnection && rowItem.modelData.connection.live)
                                          ? qsTr("In use") : ""
                                }
                                Item { Layout.fillWidth: true }
                            }

                            Text {
                                id: detailText
                                Layout.fillWidth: true
                                visible: text !== ""
                                text: rowItem.isSpace ? rowItem.modelData.space.detail
                                      : rowItem.isMember ? root.memberDetail(rowItem.modelData.member)
                                      : rowItem.isConnection ? rowItem.modelData.connection.providerName + " · " + rowItem.modelData.connection.baseUrl
                                      : ""
                                elide: Text.ElideRight
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                            }
                        }

                        // Trailing cue for what Enter does.
                        Text {
                            visible: !rowItem.isAdd
                            text: rowItem.isSpace ? Icons.chevronRight
                                  : rowItem.isMember ? Icons.moreHoriz
                                  : Icons.delete_
                            font.family: Theme.fontIcon
                            font.pixelSize: Theme.fontSizeIcon
                            color: rowItem.isConnection && rowItem.rowFocused ? Theme.errorColor
                                   : (rowItem.rowFocused ? Theme.textPrimary : Theme.textTertiary)
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    // Space actions
                    RowLayout {
                        visible: rowItem.isActions
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.leftMargin: Theme.spacingMedium
                        spacing: Theme.controlSpacing

                        Repeater {
                            id: buttonRepeater
                            model: rowItem.actions

                            ActionButton {
                                id: actionButton
                                required property var modelData
                                required property int index
                                Layout.preferredHeight: Theme.buttonHeightMedium
                                variant: modelData.variant || "secondary"
                                iconGlyph: modelData.icon || ""
                                text: modelData.text || ""
                                focusPolicy: Qt.StrongFocus

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        root.preferredColumn = index
                                        rowList.currentIndex = rowItem.index
                                    }
                                }
                                onClicked: root.triggerSpaceAction(rowItem.modelData.space, modelData.key)
                                Keys.onReturnPressed: root.triggerSpaceAction(rowItem.modelData.space, modelData.key)
                                Keys.onEnterPressed: root.triggerSpaceAction(rowItem.modelData.space, modelData.key)
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
                                Keys.onUpPressed: function(event) {
                                    if (rowItem.index === 0) {
                                        event.accepted = false
                                        return
                                    }
                                    root.moveRow(-1)
                                    event.accepted = true
                                }
                                Keys.onDownPressed: function(event) { root.moveRow(1); event.accepted = true }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- Source options -------------------------------------------------

    BloomMenu {
        id: memberMenu
        property var row: null
        property var actions: []
        property string pendingKey: ""

        function openFor(target) {
            row = target
            actions = root.memberActions(target)
            pendingKey = ""
            var item = rowList.itemAtIndex(root.rowIndexForKey(target.key))
            if (item)
                popup(item, item.width - width, 0)
            else
                open()
        }

        onOpened: {
            currentIndex = 0
            forceActiveFocus()
        }
        // Run the action after the menu has closed, so a dialog it opens
        // keeps focus.
        onClosed: {
            var key = pendingKey
            var target = row
            pendingKey = ""
            Qt.callLater(function() {
                if (key !== "")
                    root.triggerMemberAction(target, key)
                else
                    root.focusRow(root.rowIndexForKey(target ? target.key : ""))
            })
        }

        Repeater {
            model: memberMenu.actions

            BloomMenuItem {
                required property var modelData
                text: modelData.text
                iconGlyph: modelData.icon || ""
                destructive: !!modelData.destructive
                onTriggered: {
                    memberMenu.pendingKey = modelData.key
                    memberMenu.close()
                }
            }
        }
    }

    // ---- Dialogs --------------------------------------------------------

    SourceLibrariesDialog {
        id: librariesDialog
        onClosed: Qt.callLater(function() {
            var index = root.rowIndexForKey(root.restoreRowKey)
            root.focusRow(index >= 0 ? index : rowList.currentIndex)
        })
    }

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
