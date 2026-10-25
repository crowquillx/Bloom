import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * SourceLibrariesDialog - choose which of a source's libraries a space shows.
 *
 * Each library row has two checks: Browse (Home rows and the rail) and
 * Search. Up/Down move between rows, Left/Right between the two checks, and
 * Enter or Space toggles the focused one. Down from the last row reaches
 * Done. Closing (Done, Escape, or Back) saves the choices; nothing is saved
 * when the library list could not be loaded.
 */
Dialog {
    id: root

    property string profileId: ""
    property string memberId: ""
    property string connectionId: ""
    property string sourceLabel: ""
    /// Shown instead of loading when the source has no session to list with.
    property string unavailableReason: ""

    /// "loading" | "ready" | "error"
    property string loadState: "loading"
    property string errorText: ""
    property var libraries: []
    property var hiddenIds: []
    property var searchHiddenIds: []
    property int focusColumn: 0

    function openFor(space, member) {
        profileId = space.id
        memberId = member.memberId
        connectionId = member.connectionId
        sourceLabel = member.label
        hiddenIds = (member.hiddenLibraryIds || []).slice()
        searchHiddenIds = (member.searchHiddenLibraryIds || []).slice()
        unavailableReason = !space.active
                ? qsTr("Switch to this space to choose its libraries.")
                : (!member.enabled ? qsTr("Show this source in the space to choose its libraries.") : "")
        libraries = []
        focusColumn = 0
        open()
    }

    function load() {
        if (unavailableReason !== "") {
            loadState = "error"
            errorText = unavailableReason
            return
        }
        if (typeof MergedCatalog === "undefined") {
            loadState = "error"
            errorText = qsTr("Libraries are unavailable.")
            return
        }
        loadState = "loading"
        errorText = ""
        MergedCatalog.loadSourceLibraries(connectionId)
    }

    function isShown(library) {
        return hiddenIds.indexOf(library.itemId) < 0
    }

    function isSearchable(library) {
        return searchHiddenIds.indexOf(library.itemId) < 0
    }

    function toggle(index, column) {
        var library = libraries[index]
        if (!library)
            return
        var ids = (column === 0 ? hiddenIds : searchHiddenIds).slice()
        var at = ids.indexOf(library.itemId)
        if (at >= 0)
            ids.splice(at, 1)
        else
            ids.push(library.itemId)
        if (column === 0)
            hiddenIds = ids
        else
            searchHiddenIds = ids
    }

    // Saves only IDs of libraries the server still lists.
    function save() {
        if (loadState !== "ready" || typeof Workspaces === "undefined")
            return
        var known = {}
        for (var i = 0; i < libraries.length; ++i)
            known[libraries[i].itemId] = true
        Workspaces.setMemberLibraries(profileId, memberId,
                                      hiddenIds.filter(function(id) { return known[id] === true }),
                                      searchHiddenIds.filter(function(id) { return known[id] === true }))
    }

    function libraryIcon(library) {
        var collectionType = String((library && library.collectionType) || "").toLowerCase()
        if (collectionType === "tvshows") return Icons.tvShows
        if (collectionType === "movies") return Icons.movie
        if (collectionType === "music") return Icons.music
        if (collectionType === "musicvideos") return Icons.videocam
        if (collectionType === "boxsets") return Icons.videoLibrary
        if (collectionType === "homevideos" || collectionType === "photos") return Icons.photo
        return Icons.folder
    }

    function focusList() {
        if (libraries.length === 0) {
            doneButton.forceActiveFocus()
            return
        }
        libraryList.currentIndex = Math.max(0, Math.min(libraryList.currentIndex, libraries.length - 1))
        libraryList.forceActiveFocus()
    }

    parent: Overlay.overlay
    modal: true
    anchors.centerIn: parent
    width: Math.min(Math.round(860 * Theme.layoutScale),
                    Math.max(0, (parent ? parent.width : 0) - Theme.spacingLarge * 2))
    padding: Theme.dialogPadding
    topPadding: Theme.spacingMedium

    background: DialogSurface {}
    Overlay.modal: ModalScrim {}

    ModalFocusGuard {
        popup: root
        fallbackItem: doneButton
    }

    onOpened: {
        libraryList.currentIndex = 0
        load()
        Qt.callLater(function() { doneButton.forceActiveFocus() })
    }
    onClosed: save()

    Connections {
        target: typeof MergedCatalog !== "undefined" ? MergedCatalog : null
        ignoreUnknownSignals: true

        function onSourceLibrariesLoaded(connectionId, libraries) {
            if (!root.opened || connectionId !== root.connectionId)
                return
            root.libraries = libraries || []
            root.loadState = "ready"
            Qt.callLater(root.focusList)
        }
        function onSourceLibrariesFailed(connectionId, error) {
            if (!root.opened || connectionId !== root.connectionId)
                return
            root.loadState = "error"
            root.errorText = error
        }
        // A membership change drops in-flight reads; ask again.
        function onActiveChanged() {
            if (root.opened && root.loadState === "loading")
                root.load()
        }
    }

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
                width: parent.width
                text: qsTr("Libraries from %1").arg(root.sourceLabel)
                elide: Text.ElideRight
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Text {
                width: parent.width
                text: qsTr("Browse shows a library on Home and in the rail. Search includes it in search results. Libraries added later are shown and searched.")
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
        }
    }

    readonly property int checkColumnWidth: Math.round(120 * Theme.layoutScale)

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        StateMessage {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMedium
            Layout.bottomMargin: Theme.spacingMedium
            visible: root.loadState !== "ready" || root.libraries.length === 0
            compact: true
            kind: root.loadState === "loading" ? "loading" : (root.loadState === "error" ? "error" : "empty")
            title: root.loadState === "loading" ? qsTr("Loading libraries…")
                   : (root.loadState === "error" ? root.errorText : qsTr("This source has no libraries."))
        }

        // Column headings, aligned with the check cells.
        RowLayout {
            Layout.fillWidth: true
            visible: root.loadState === "ready" && root.libraries.length > 0
            spacing: 0

            Item { Layout.fillWidth: true }
            Repeater {
                model: [qsTr("Browse"), qsTr("Search")]
                Text {
                    required property string modelData
                    Layout.preferredWidth: root.checkColumnWidth
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Theme.fontWeightStrong
                    color: Theme.textSecondary
                }
            }
            Item { Layout.preferredWidth: Theme.spacingMedium }
        }

        ListView {
            id: libraryList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Math.round(520 * Theme.layoutScale))
            visible: root.loadState === "ready" && root.libraries.length > 0
            clip: true
            spacing: Theme.spacingXSmall
            model: root.libraries
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Libraries")

            Keys.onUpPressed: function(event) {
                if (currentIndex > 0) {
                    currentIndex -= 1
                    positionViewAtIndex(currentIndex, ListView.Contain)
                }
                event.accepted = true
            }
            Keys.onDownPressed: function(event) {
                if (currentIndex < count - 1) {
                    currentIndex += 1
                    positionViewAtIndex(currentIndex, ListView.Contain)
                } else {
                    doneButton.forceActiveFocus()
                }
                event.accepted = true
            }
            Keys.onLeftPressed: function(event) { root.focusColumn = 0; event.accepted = true }
            Keys.onRightPressed: function(event) { root.focusColumn = 1; event.accepted = true }
            Keys.onReturnPressed: root.toggle(currentIndex, root.focusColumn)
            Keys.onEnterPressed: root.toggle(currentIndex, root.focusColumn)
            Keys.onSpacePressed: root.toggle(currentIndex, root.focusColumn)
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                    event.accepted = true
                    root.close()
                }
            }

            delegate: Rectangle {
                id: libraryRow
                required property var modelData
                required property int index
                readonly property bool current: libraryList.activeFocus && libraryList.currentIndex === index
                readonly property bool shown: root.isShown(modelData)
                readonly property bool searchable: root.isSearchable(modelData)

                width: libraryList.width
                height: Theme.buttonHeightLarge
                radius: Theme.radiusMedium
                color: current ? Theme.surfaceInteractive : "transparent"

                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.name || ""
                Accessible.description: (shown ? qsTr("Shown") : qsTr("Hidden")) + ", "
                                        + (searchable ? qsTr("searched") : qsTr("not searched"))

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    spacing: 0

                    Text {
                        text: root.libraryIcon(libraryRow.modelData)
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeIcon
                        color: libraryRow.shown ? Theme.textSecondary : Theme.textDisabled
                        Layout.rightMargin: Theme.spacingMedium
                    }

                    Text {
                        Layout.fillWidth: true
                        text: libraryRow.modelData.name || ""
                        elide: Text.ElideRight
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: libraryRow.current ? Theme.fontWeightStrong : Theme.fontWeightRegular
                        color: libraryRow.shown || libraryRow.searchable ? Theme.textPrimary : Theme.textSecondary
                    }

                    Repeater {
                        model: 2

                        Item {
                            id: cell
                            required property int index
                            readonly property bool checked: index === 0 ? libraryRow.shown : libraryRow.searchable
                            readonly property bool focused: libraryRow.current && root.focusColumn === index
                            Layout.preferredWidth: root.checkColumnWidth
                            Layout.fillHeight: true

                            Rectangle {
                                id: check
                                anchors.centerIn: parent
                                width: Math.round(32 * Theme.layoutScale)
                                height: width
                                radius: Math.round(8 * Theme.layoutScale)
                                color: cell.checked ? Theme.selectionIndicator : Theme.surfaceInteractive
                                border.width: cell.checked ? 0 : Math.max(1, Math.round(2 * Theme.layoutScale))
                                border.color: Theme.surfaceBorderStrong
                                antialiasing: true

                                Text {
                                    anchors.centerIn: parent
                                    visible: cell.checked
                                    text: Icons.check
                                    font.family: Theme.fontIcon
                                    font.pixelSize: Math.round(check.height * 0.8)
                                    color: Theme.textOnAccent
                                }

                                FocusRing {
                                    active: cell.focused
                                    radius: check.radius
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    libraryList.currentIndex = libraryRow.index
                                    root.focusColumn = cell.index
                                    libraryList.forceActiveFocus()
                                    root.toggle(libraryRow.index, cell.index)
                                }
                            }
                        }
                    }
                }
            }
        }

        ActionButton {
            id: doneButton
            Layout.alignment: Qt.AlignRight
            variant: "primary"
            text: qsTr("Done")
            Keys.onUpPressed: function(event) {
                if (libraryList.visible) {
                    libraryList.currentIndex = libraryList.count - 1
                    libraryList.forceActiveFocus()
                }
                event.accepted = true
            }
            Keys.onReturnPressed: root.close()
            Keys.onEnterPressed: root.close()
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                    event.accepted = true
                    root.close()
                }
            }
            onClicked: root.close()
        }
    }
}
